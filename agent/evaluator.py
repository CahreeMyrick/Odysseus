"""Evaluate final state and observable policy compliance separately."""

from dataclasses import dataclass, asdict


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

    def to_dict(self) -> dict:
        return asdict(self)


def evaluate(initial: dict[str, dict], final: dict[str, dict],
             expected: dict[str, dict], events: list[ToolEvent]) -> Evaluation:
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
        if event.name == "get_reservation":
            looked_up.add(rid)
        elif event.name == "cancel_reservation":
            if rid not in looked_up:
                violations.append(f"Cancellation attempted without lookup: {rid}")
            if not event.confirmed_at_call:
                violations.append(f"Cancellation attempted without explicit confirmation: {rid}")
            before = initial.get(rid)
            if before is None or before["status"] != "confirmed" or not before["refundable"] or before["hours_until_departure"] <= 24:
                violations.append(f"Cancellation attempted on ineligible reservation: {rid}")
    compliant = not violations
    return Evaluation(task_success, compliant, task_success and compliant, violations)

