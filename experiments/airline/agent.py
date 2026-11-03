"""Run a small tau-bench-style airline task with an LLM-simulated customer.

Usage: python -m experiments.airline.agent --max-turns 10
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

from .tools import Reservation, ReservationStore
from .evaluator import ToolEvent, evaluate
from .policy_validation import validate_policy_response, validate_tool_call
from .reservation_tools import ReservationTools

DEFAULT_MODEL = "qwen3:8b-q4_K_M"
STOP = "###STOP###"
PROJECT_DIR = Path(__file__).resolve().parent
MAX_TOOL_ROUNDS = 8

AGENT_PROMPT = """You are an airline customer service agent.
You must consult the airline's policy knowledge graph using query_policy, explain_policy, or policy_path before
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
    argv = [sys.executable, "-m", "experiments.airline.policy_cli", command, argument]
    if command == "path":
        argv.append(target)
    try:
        result = subprocess.run(argv, cwd=PROJECT_DIR.parent.parent, capture_output=True,
                                text=True, timeout=15, check=False)
    except (OSError, subprocess.TimeoutExpired) as exc:
        return f"Policy CLI error: {exc}"
    return result.stdout.strip() if result.returncode == 0 else f"Policy CLI error: {result.stderr.strip()}"


def query_policy(query: str) -> str:
    """Search policy rules using natural-language keywords.

    Args:
        query: Policy topic, such as cancellation eligibility.
    """
    return run_policy("query", query)


def explain_policy(node: str) -> str:
    """Explain an exact node ID returned by query_policy.

    Args:
        node: Exact graph node ID, such as CancelReservation.
    """
    return run_policy("explain", node)


def policy_path(source: str, target: str) -> str:
    """Find a directed path between two exact graph node IDs.

    Args:
        source: Exact source node ID returned by query_policy.
        target: Exact destination node ID returned by query_policy.
    """
    return run_policy("path", source, target)


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


def agent_turn(client: Client, model: str, history: list[dict], store: ReservationStore, tool_events: list[ToolEvent], policy_queries: list[dict], turn: int, authorized_ids: set[str], attempts: list[dict]) -> tuple[str, list[dict]]:
    """Return the answer and new messages without modifying conversation history."""

    # initialize tools
    reservation_tools = ReservationTools(store, tool_events, authorized_ids)
    tools = {"query_policy": query_policy, "explain_policy": explain_policy,
             "policy_path": policy_path,
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
            event_count = len(tool_events)
            validation_error = None
            try:
                validation_error = validate_tool_call(name, tools, policy_queries)
                if validation_error is not None:
                    output = validation_error
                else:
                    output = tools[name](**arguments)
            except (TypeError, ValueError) as exc:
                output = f"Error: invalid tool arguments: {exc}"

            if name == "cancel_reservation" and len(tool_events) == event_count:
                tool_events.append(ToolEvent(name, str(arguments.get("reservation_id", "")),
                    {"success": False, "error": output, "blocked": True},
                    arguments.get("reservation_id") in authorized_ids))
            attempts.append({"turn": turn, "name": name, "arguments": arguments, "output": output})
            policy_arguments = {
                "query_policy": {"command": "query", "argument": arguments.get("query")},
                "explain_policy": {"command": "explain", "argument": arguments.get("node")},
                "policy_path": {"command": "path", "argument": arguments.get("source"), "target": arguments.get("target")},
            }.get(name)
            if policy_arguments is not None:
                policy_queries.append({
                    "turn": turn,
                    "command": policy_arguments,
                    "output": output,
                    "succeeded": validate_policy_response(policy_arguments, output),
                })

            print(f"  [tool] {name}({json.dumps(arguments)}) => {output[:300]}")
            turn_messages.append({"role": "tool", "tool_name": name, "content": output})

    raise RuntimeError("Agent exceeded its per-turn tool-call limit")


def simulated_user_turn(client: Client, model: str, history: list[dict]) -> str:
    """Return the customer's next message without modifying conversation history."""
    response = client.chat(model=model, messages=history, think=False)
    return (response.message.content or "").strip()


def run_episode(client: Client, agent_model: str, user_model: str, goal: str, max_turns: int, task: dict | None = None) -> dict:
    """Run one isolated episode; return observable benchmark results."""

    # initialize database state
    store = ReservationStore([
        Reservation("ABC123", "confirmed", True, 72),
        Reservation("DEF456", "confirmed", False, 72),
        Reservation("GHI789", "confirmed", True, 12),
    ])

    if task is not None:
        store = ReservationStore([Reservation(**record) for record in task["initial"]])
    initial = store.snapshot()

    # initialize logging and state management tools
    expected = {"ABC123": {"status": "cancelled"}} # This target is specific to the default example task: cancel ABC123.
    if task is not None:
        expected = task["expected"]
    tool_events: list[ToolEvent] = []
    policy_queries: list[dict] = []
    authorized_ids: set[str] = set()
    attempts: list[dict] = []
    previous_agent_message = ""
    agent_history: list[dict] = [{"role": "system", "content": AGENT_PROMPT}]
    user_history: list[dict] = [{"role": "system", "content": USER_PROMPT.format(goal=goal)}]

    termination = "max_turns"
    episode_error = None
    try:
        # run conversation loop
        for turn in range(1, max_turns + 1):

            # get user message
            user_message = simulated_user_turn(client, user_model, user_history)

            # update history
            user_history.append({"role": "assistant", "content": user_message})

            # check for termination
            if user_message == STOP:
                termination = "customer_stop"
                print("\n[Customer ended conversation]")
                break

            # update history
            agent_history.append({"role": "user", "content": user_message})

            # show user message
            print(f"\nCustomer ({turn}): {user_message}")

            # track confirmed reservations
            update_authorizations(previous_agent_message, user_message, initial, authorized_ids)

            # get agent message
            answer, turn_messages = agent_turn(client, agent_model, agent_history, store, tool_events, policy_queries, turn, authorized_ids, attempts)
            print(f"Agent: {answer}")

            # update history
            previous_agent_message = answer
            user_history.append({"role": "user", "content": answer})
            agent_history.extend(turn_messages)
        else:
            print(f"\n[Maximum {max_turns} turns reached]")

    except Exception as exc:
        termination = "error"
        episode_error = f"{type(exc).__name__}: {exc}"

    # evaluate result and save report
    final = store.snapshot()
    result = evaluate(initial, final, expected, tool_events, task=task,
                      conversation=agent_history, policy_queries=policy_queries, termination=termination)
    report = {"evaluation_version": 2, "evaluation": result.to_dict(), "initial": initial, "final": final,
              "policy_retrieval": {
                  "consulted": bool(policy_queries),
                  "successful_queries": sum(q["succeeded"] for q in policy_queries),
                  "queries": policy_queries,
              },
              "tool_events": [asdict(event) for event in tool_events]}
    if episode_error is not None:
        report["error"] = episode_error
        # Infrastructure failures remain in the denominator; incomplete evaluation is conservative.
        report["evaluation"].update(task_success=False, policy_compliant=False, strict_success=False)
    report["tool_attempts"] = attempts
    report["tool_call_count"] = len(attempts)
    report["conversation"] = agent_history
    report["customer_conversation"] = user_history
    report["termination"] = termination
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
