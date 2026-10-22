"""Run a small tau-bench-style airline task with an LLM-simulated customer.

Usage: uv run python agent.py --max-turns 10
"""

import argparse
import json
import re
import subprocess
import sys
from collections.abc import Iterable
from dataclasses import asdict
from pathlib import Path

from ollama import Client

from tools import Reservation, ReservationStore
from evaluator import ToolEvent, evaluate
from policy_validation import validate_policy_response, validate_tool_call
from reservation_tools import ReservationTools

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


def update_authorizations(agent_message: str, customer_message: str, reservation_ids: Iterable[str], authorized_ids: set[str]) -> None:
    """Record explicit cancellation confirmations from the latest customer reply."""
    for reservation_id in reservation_ids:
        if confirmed_cancellation(agent_message, customer_message, reservation_id):
            authorized_ids.add(reservation_id)


def agent_turn(client: Client, model: str, history: list[dict], store: ReservationStore, tool_events: list[ToolEvent], policy_queries: list[dict], turn: int, authorized_ids: set[str]) -> tuple[str, list[dict]]:
    """Return the answer and new messages without modifying conversation history."""

    # initialize tools
    reservation_tools = ReservationTools(store, tool_events, authorized_ids)
    tools = {"run_policy": run_policy,
             "get_reservation": reservation_tools.get_reservation,
             "cancel_reservation": reservation_tools.cancel_reservation}

    turn_messages: list[dict] = []

    for _ in range(MAX_TOOL_ROUNDS):

        # get agent response from ollama client
        response = client.chat(model=model, messages=history + turn_messages, tools=list(tools.values()), think=False)
        message = response.message
        turn_messages.append(message.model_dump(exclude_none=True))
        if not message.tool_calls:
            return message.content or "(No response)", turn_messages

        for call in message.tool_calls:
            name = call.function.name
            arguments = call.function.arguments
            try:
                validation_error = validate_tool_call(name, tools, policy_queries)
                if validation_error is not None:
                    output = validation_error
                else:
                    output = tools[name](**arguments)
            except (TypeError, ValueError) as exc:
                output = f"Error: invalid tool arguments: {exc}"

            if name == "run_policy":
                policy_queries.append({
                    "turn": turn,
                    "command": arguments,
                    "output": output,
                    "succeeded": validate_policy_response(arguments, output),
                })

            print(f"  [tool] {name}({json.dumps(arguments)}) => {output[:300]}")
            turn_messages.append({"role": "tool", "tool_name": name, "content": output})

    raise RuntimeError("Agent exceeded its per-turn tool-call limit")


def simulated_user_turn(client: Client, model: str, history: list[dict]) -> str:
    """Return the customer's next message without modifying conversation history."""
    response = client.chat(model=model, messages=history, think=False)
    return (response.message.content or "").strip()


def run_episode(client: Client, agent_model: str, user_model: str, goal: str, max_turns: int) -> dict:
    """Run one isolated episode; return observable benchmark results."""

    # initialize database state
    store = ReservationStore([
        Reservation("ABC123", "confirmed", True, 72),
        Reservation("DEF456", "confirmed", False, 72),
        Reservation("GHI789", "confirmed", True, 12),
    ])

    initial = store.snapshot()

    # initialize logging and state management tools
    expected = {"ABC123": {"status": "cancelled"}} # This target is specific to the default example task: cancel ABC123.
    tool_events: list[ToolEvent] = []
    policy_queries: list[dict] = []
    authorized_ids: set[str] = set()
    previous_agent_message = ""
    agent_history: list[dict] = [{"role": "system", "content": AGENT_PROMPT}]
    user_history: list[dict] = [{"role": "system", "content": USER_PROMPT.format(goal=goal)}]

    # run conversation loop
    for turn in range(1, max_turns + 1):

        # get user message
        user_message = simulated_user_turn(client, user_model, user_history)

        # update history
        user_history.append({"role": "assistant", "content": user_message})

        # check for termination
        if user_message == STOP:
            print("\n[Customer ended conversation]")
            break

        # update history
        agent_history.append({"role": "user", "content": user_message})

        # show user message
        print(f"\nCustomer ({turn}): {user_message}")

        # track confirmed reservations
        update_authorizations(previous_agent_message, user_message, initial, authorized_ids)

        # get agent message
        answer, turn_messages = agent_turn(client, agent_model, agent_history, store, tool_events, policy_queries, turn, authorized_ids)
        print(f"Agent: {answer}")

        # update history
        previous_agent_message = answer
        user_history.append({"role": "user", "content": answer})
        agent_history.extend(turn_messages)
    else:
        print(f"\n[Maximum {max_turns} turns reached]")

    # evaluate result and save report
    final = store.snapshot()
    result = evaluate(initial, final, expected, tool_events)
    report = {"evaluation": result.to_dict(), "initial": initial, "final": final,
              "policy_retrieval": {
                  "consulted": bool(policy_queries),
                  "successful_queries": sum(q["succeeded"] for q in policy_queries),
                  "queries": policy_queries,
              },
              "tool_events": [asdict(event) for event in tool_events]}
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
