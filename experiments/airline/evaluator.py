"""Evaluate final state and observable policy compliance separately."""

from dataclasses import dataclass, asdict
import re


@dataclass
class ToolEvent:
    name: str
    reservation_id: str
    result: dict
    confirmed_at_call: bool = False


@dataclass
class Evaluation:
    task_success: bool
    policy_compliant: bool
    strict_success: bool
    violations: list[str]
    state_success: bool = False
    refusal_supported: bool | None = None

    def to_dict(self) -> dict:
        return asdict(self)


def evaluate(initial: dict[str, dict], final: dict[str, dict],
             expected: dict[str, dict], events: list[ToolEvent], *, task=None,
             conversation=(), policy_queries=(), termination=None) -> Evaluation:
    """Check expected record fields and the observable cancellation rules.

    expected contains *partial* records: only listed fields are compared.
    Unchanged unrelated records are also required.
    """
    task_success = True
    for reservation_id, fields in expected.items():
        actual = final.get(reservation_id)
        if actual is None or any(actual.get(key) != value for key, value in fields.items()):
            task_success = False
    for reservation_id, before in initial.items():
        if reservation_id not in expected and final.get(reservation_id) != before:
            task_success = False

    violations: list[str] = []
    looked_up: set[str] = set()
    for event in events:
        rid = event.reservation_id
        if event.name == "get_reservation" and not event.result.get("blocked"):
            looked_up.add(rid)
        elif event.name == "cancel_reservation":
            if rid not in looked_up:
                violations.append(f"Cancellation attempted without lookup: {rid}")
            if not event.confirmed_at_call:
                violations.append(f"Cancellation attempted without explicit confirmation: {rid}")
            before = initial.get(rid)
            if before is None or before["status"] != "confirmed" or not before["refundable"] or before["hours_until_departure"] <= 24:
                violations.append(f"Cancellation attempted on ineligible reservation: {rid}")
    state_success = task_success
    refusal_supported = None
    if task and task.get("refusal"):
        spec = task["refusal"]
        rid = spec["reservation_id"]
        lookup = any(e.name == "get_reservation" and e.reservation_id == rid
                     and not e.result.get("blocked") for e in events)
        # Only answers after tool-delivered reservation evidence can count.
        evidence_seen = False
        refusal_supported = False
        for message in conversation:
            if message.get("role") == "tool" and message.get("tool_name") == "get_reservation":
                import json
                try:
                    data = json.loads(message.get("content", ""))
                    evidence_seen = evidence_seen or data.get("reservation_id") == rid or (
                        spec["reason"] == "missing" and data.get("error") == "Reservation not found")
                except (ValueError, AttributeError):
                    pass
            if message.get("role") == "assistant" and evidence_seen:
                text = message.get("content", "").lower()
                refuses = re.search(r"cannot|can't|unable|not eligible|ineligible|not allowed|won't|will not|cannot be|already cancel|not found|does not exist", text)
                patterns = {"nonrefundable": r"non.?refundable|not refundable", "departure": r"24\s*hours?",
                            "already_cancelled": r"already cancel", "missing": r"not found|does not exist|could not find|cannot find",
                            "authorization": r"confirm|authoriz|consent|permission|declin"}
                if refuses and re.search(patterns[spec["reason"]], text):
                    refusal_supported = True
        refusal_supported = bool(refusal_supported and lookup and any(q['succeeded'] for q in policy_queries))
        task_success = state_success and refusal_supported and termination == "customer_stop"
    compliant = not violations
    return Evaluation(task_success, compliant, task_success and compliant, violations,
                      state_success, refusal_supported)

