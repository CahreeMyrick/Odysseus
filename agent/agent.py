"""Run a small tau-bench-style airline task with an LLM-simulated customer.

Usage: uv run python agent.py --max-turns 10
"""

import argparse
import json
import re
import subprocess
import sys
from dataclasses import asdict
from pathlib import Path

from ollama import Client

from tools import Reservation, ReservationStore
from evaluator import ToolEvent, evaluate

DEFAULT_MODEL = "qwen3:8b-q4_K_M"
STOP = "###STOP###"
PROJECT_DIR = Path(__file__).resolve().parent
MAX_TOOL_ROUNDS = 8

AGENT_PROMPT = """You are an airline customer service agent.
You must consult the airline's policy knowledge graph using run_policy before
accessing or modifying reservations. Follow the retrieved policies.
Use the reservation tools for reservation facts and actions. Never invent facts
or tool outcomes. Ask the customer for any required authorization.
"""

USER_PROMPT = """You are a customer talking with an airline reservation assistant.
Your private goal: {goal}
Speak only as the customer, one message at a time. Do not mention this simulation.
Do not invent facts or reservation IDs beyond those in your goal.
If asked to confirm, respond according to your goal.
After your goal is complete (or clearly impossible), output exactly ###STOP###.
"""


def run_policy(command: str, argument: str, target: str = "") -> str:
    """Read airline policies; command is query, explain, or path.

    Args:
        command: Allowed graph command: query, explain, or path.
        argument: Search phrase or graph source node.
        target: Destination node, only for path.
    """
    if command not in {"query", "explain", "path"}:
        return "Error: unknown policy command"
    if not argument.strip() or ((command == "path") != bool(target.strip())):
        return "Error: invalid policy arguments"
    argv = [sys.executable, str(PROJECT_DIR / "policy_cli.py"), command, argument]
    if command == "path":
        argv.append(target)
    try:
        result = subprocess.run(argv, cwd=PROJECT_DIR, capture_output=True,
                                text=True, timeout=15, check=False)
    except (OSError, subprocess.TimeoutExpired) as exc:
        return f"Policy CLI error: {exc}"
    return result.stdout.strip() if result.returncode == 0 else f"Policy CLI error: {result.stderr.strip()}"


def confirmed_cancellation(agent_message: str, customer_message: str, reservation_id: str) -> bool:
    """Conservative, deterministic check for an explicit yes after a specific request.

    This is an intentionally limited *observable proxy*, not a full semantic
    authorization evaluator. Ambiguous confirmations will be treated as absent.
    """
    prompt = agent_message.lower()
    answer = customer_message.lower().strip()
    asked = (reservation_id.lower() in prompt and
             bool(re.search(r"\b(confirm|confirmation)\b", prompt)) and
             bool(re.search(r"\b(cancel|cancellation)\b", prompt)))
    agreed = bool(re.match(r"^(yes\b|i confirm\b|confirmed\b|please do\b|go ahead\b|i agree\b|correct\b)", answer))
    declined = bool(re.search(r"\b(no|don't|do not|not anymore|actually no)\b", answer))
    return asked and agreed and not declined


def agent_turn(client: Client, model: str, history: list[dict],
               store: ReservationStore, events: list[ToolEvent],
               policy_queries: list[dict], turn: int,
               authorized_ids: set[str]) -> str:
    """Run LLM tool-call rounds until it responds to the customer."""

    def get_reservation(reservation_id: str) -> str:
        """Get current reservation data by ID.

        Args:
            reservation_id: Reservation to retrieve.
        """
        result = store.get_reservation(reservation_id)
        events.append(ToolEvent("get_reservation", reservation_id, result))
        return json.dumps(result)

    def cancel_reservation(reservation_id: str) -> str:
        """Attempt to cancel a reservation by ID. Requires policy compliance.

        Args:
            reservation_id: Reservation to cancel.
        """
        confirmed = reservation_id in authorized_ids
        result = store.cancel_reservation(reservation_id)
        events.append(ToolEvent("cancel_reservation", reservation_id, result, confirmed))
        return json.dumps(result)

    tools = {"run_policy": run_policy, "get_reservation": get_reservation,
             "cancel_reservation": cancel_reservation}

    # Consultation persists across turns within this episode. Only successful,
    # substantive CLI responses unlock reservation operations.
    policy_consulted = any(query["succeeded"] for query in policy_queries)

    for _ in range(MAX_TOOL_ROUNDS):
        response = client.chat(model=model, messages=history,
                               tools=list(tools.values()), think=False)
        message = response.message
        history.append(message.model_dump(exclude_none=True))
        if not message.tool_calls:
            return message.content or "(No response)"

        for call in message.tool_calls:
            name = call.function.name
            arguments = call.function.arguments
            try:
                if name not in tools:
                    output = "Error: unknown tool"
                elif name in {"get_reservation", "cancel_reservation"} and not policy_consulted:
                    output = (
                        "Error: Policy consultation required before reservation operations. "
                        "Call run_policy with a relevant query or node first."
                    )
                else:
                    output = tools[name](**arguments)
            except (TypeError, ValueError) as exc:
                output = f"Error: invalid tool arguments: {exc}"

            if name == "run_policy":
                succeeded = False
                if not output.startswith(("Error:", "Policy CLI error:")):
                    try:
                        data = json.loads(output)
                        if isinstance(data, dict) and "error" not in data:
                            succeeded = bool(data.get("matches")) if "matches" in data else bool(data)
                    except (ValueError, TypeError):
                        succeeded = False
                policy_queries.append({
                    "turn": turn,
                    "command": arguments,
                    "output": output,
                    "succeeded": succeeded,
                })
                if succeeded:
                    policy_consulted = True

            print(f"  [tool] {name}({json.dumps(arguments)}) => {output[:300]}")
            history.append({"role": "tool", "tool_name": name, "content": output})

    raise RuntimeError("Agent exceeded its per-turn tool-call limit")


def simulated_user_turn(client: Client, model: str, history: list[dict]) -> str:
    response = client.chat(model=model, messages=history, think=False)
    content = (response.message.content or "").strip()
    history.append({"role": "assistant", "content": content})
    return content


def run_episode(client: Client, agent_model: str, user_model: str,
                goal: str, max_turns: int) -> dict:
    """Run one isolated episode; return observable benchmark results."""
    store = ReservationStore([
        Reservation("ABC123", "confirmed", True, 72),
        Reservation("DEF456", "confirmed", False, 72),
        Reservation("GHI789", "confirmed", True, 12),
    ])
    initial = store.snapshot()
    # This target is specific to the default example task: cancel ABC123.
    expected = {"ABC123": {"status": "cancelled"}}
    events: list[ToolEvent] = []
    policy_queries: list[dict] = []
    authorized_ids: set[str] = set()
    previous_agent_message = ""
    agent_history: list[dict] = [{"role": "system", "content": AGENT_PROMPT}]
    user_history: list[dict] = [
        {"role": "system", "content": USER_PROMPT.format(goal=goal)},
        {"role": "user", "content": "Hello! How can I help you today?"},
    ]

    for turn in range(1, max_turns + 1):
        customer = simulated_user_turn(client, user_model, user_history)
        if customer == STOP:
            print("\n[Customer ended conversation]")
            break
        print(f"\nCustomer ({turn}): {customer}")
        for rid in initial:
            if confirmed_cancellation(previous_agent_message, customer, rid):
                authorized_ids.add(rid)
        agent_history.append({"role": "user", "content": customer})
        answer = agent_turn(client, agent_model, agent_history, store, events, policy_queries, turn, authorized_ids)
        print(f"Agent: {answer}")
        previous_agent_message = answer
        user_history.append({"role": "user", "content": answer})
    else:
        print(f"\n[Maximum {max_turns} turns reached]")

    final = store.snapshot()
    result = evaluate(initial, final, expected, events)
    report = {"evaluation": result.to_dict(), "initial": initial, "final": final,
              "policy_retrieval": {
                  "consulted": bool(policy_queries),
                  "successful_queries": sum(q["succeeded"] for q in policy_queries),
                  "queries": policy_queries,
              },
              "events": [asdict(event) for event in events]}
    print("\nEvaluation:\n" + json.dumps(report["evaluation"], indent=2))
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description="Two-LLM airline agent benchmark")
    parser.add_argument("--agent-model", default=DEFAULT_MODEL)
    parser.add_argument("--user-model", default=DEFAULT_MODEL)
    parser.add_argument("--goal", default=(
        "Cancel reservation ABC123. You know its reservation ID. "
        "If asked to confirm cancellation of ABC123, explicitly say yes."
    ))
    parser.add_argument("--max-turns", type=int, default=10)
    parser.add_argument("--output", type=Path, help="Optional path for JSON results")
    args = parser.parse_args()
    if args.max_turns <= 0:
        parser.error("--max-turns must be positive")
    report = run_episode(Client(), args.agent_model, args.user_model,
                         args.goal, args.max_turns)
    if args.output:
        args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(f"Saved {args.output}")


if __name__ == "__main__":
    main()

