"""Deterministic notebook evaluation and isolated customer conversations."""

import operator
import re
from copy import deepcopy

from pydantic import BaseModel, ConfigDict, field_validator
from .tools import Reservation, ReservationStore


class CustomerTurn(BaseModel):
    model_config = ConfigDict(extra="forbid", strict=True)
    message: str
    done: bool

    @field_validator("message")
    @classmethod
    def no_control_tokens(cls, value):
        if not value.strip() or "###STOP###" in value:
            raise ValueError("Provide a customer message without stop tokens")
        return value


CUSTOMER_PROMPT = """You simulate a customer in an airline service conversation.
Your goal: {goal}
Return JSON with message (your natural customer reply) and done (a boolean).
Use only reservation IDs and facts supplied in your goal or the conversation.
Never put control tokens such as ###STOP### in message.
Set done=false when requesting an action, answering a clarification, or confirming.
Confirmation alone is NOT completion. Set done=true only after the agent reports
the final outcome or explains why the goal cannot be achieved, or you abandon it.
When asked to confirm, follow your goal; do not claim cancellation already occurred.
"""


def simulated_customer_turn(client, model, history):
    response = client.chat(model=model, messages=history,
                           format=CustomerTurn.model_json_schema(), think=False)
    return CustomerTurn.model_validate_json(response.message.content)


def is_confirmation(message, reservation_id):
    """Small whole-message grammar; excludes extra targets, negation and qualifiers."""
    text = message.strip().lower().rstrip(".!")
    target = re.escape(reservation_id.lower())
    return re.fullmatch(
        rf"(?:yes|yes,? please|(?:yes[, ]+)?(?:please )?cancel (?:it|(?:reservation )?{target})|"
        rf"(?:yes[, ]+)?(?:i )?confirm(?: cancellation of)? (?:reservation )?{target})",
        text,
    ) is not None

ATTRIBUTE_FIELDS = {
    "ReservationStatus": "status", "Refundable": "refundable",
    "HoursUntilDeparture": "hours_until_departure", "ReservationID": "reservation_id",
}
OPERATORS = {"==": operator.eq, "!=": operator.ne, ">": operator.gt,
             "<": operator.lt, ">=": operator.ge, "<=": operator.le}
DEFAULT_RECORDS = [
    Reservation("ABC123", "confirmed", True, 48),
    Reservation("DEF456", "confirmed", False, 48),
    Reservation("GHI789", "confirmed", True, 12),
]


def evaluate_policy(context, record, authorized=False):
    """Evaluate retrieved rules; unsupported or missing facts fail closed."""
    results = []
    for condition in context.conditions:
        props = condition.properties
        predicate = props.get("predicate")
        passed = False
        error = None
        actual = None
        if predicate == "exists(reservation_id)":
            actual = record.get("success") is True
            passed = actual
        elif predicate == "explicit_confirmation(reservation_id)":
            actual = authorized
            passed = authorized is True
        elif predicate:
            error = f"Unsupported predicate: {predicate}"
        else:
            field = ATTRIBUTE_FIELDS.get(props.get("attribute"))
            compare = OPERATORS.get(props.get("operator"))
            actual = record.get(field) if field else None
            expected = props.get("value")
            if not field or compare is None or "value" not in props:
                error = "Unsupported comparison"
            elif record.get("success") is not True or field not in record:
                error = "Required reservation fact is unavailable"
            elif type(actual) is not type(expected) and not (
                type(actual) in (int, float) and type(expected) in (int, float)
            ):
                error = "Reservation fact has an incompatible type"
            else:
                passed = bool(compare(actual, expected))
        results.append({"condition": condition.node_id,
                        "description": props.get("description", condition.node_id),
                        "category": "authorization" if condition.node_id == "HasAuthorization" else "eligibility",
                        "passed": passed, "actual": actual, "error": error})
    eligibility = [r for r in results if r["category"] == "eligibility"]
    return {
        "conditions": results,
        "eligible": bool(eligibility) and all(r["passed"] for r in eligibility),
        "authorized": authorized is True,
        "execution_allowed": context.operation == "cancel_reservation"
            and bool(results) and all(r["passed"] for r in results),
    }


class NotebookSession:
    """The interpreter and retriever are injected from notebook cells.

    Confirmation accepts a bounded whole-message grammar after the controller's
    question. Other messages clear pending consent and never grant authorization.
    """

    def __init__(self, interpret, retrieve, graph, records=None):
        self.interpret = interpret
        self.retrieve = retrieve
        self.graph = graph
        self.store = ReservationStore(deepcopy(DEFAULT_RECORDS if records is None else records))
        self.initial = self.store.snapshot()
        self.transcript = []
        self.events = []
        self.pending = None
        self.completed = False
        self.outcome = None

    def finish(self, outcome, message):
        self.completed = True
        self.pending = None
        self.outcome = outcome
        self.events.append({"kind": "outcome", "outcome": outcome})
        return message

    def reply(self, message):
        if self.completed:
            return "This session is complete. Start a new session for another request."
        prior = list(self.transcript)
        self.transcript.append({"role": "user", "content": message})
        try:
            answer = self._reply(message, prior)
        except Exception as exc:
            self.pending = None
            self.events.append({"kind": "error", "error": f"{type(exc).__name__}: {exc}"})
            raise
        self.transcript.append({"role": "assistant", "content": answer})
        return answer

    def _reply(self, message, history):
        pending, self.pending = self.pending, None
        authorized = False
        if pending is not None:
            query, reservation_id = pending
            normalized = message.strip().lower().rstrip('.!')
            if normalized in {"no", "no thanks", "do not cancel", "don't cancel"}:
                return self.finish("declined", "Understood. I have not cancelled the reservation.")
            authorized = is_confirmation(message, reservation_id)
            if not authorized:
                query = self.interpret(message, history)
        else:
            query = self.interpret(message, history)

        # Always retrieve again, including after confirmation.
        context = self.retrieve(query, self.graph)
        self.events.append({"kind": "policy", "context": context.model_dump()})
        target = next(e for e in query.entities if e.alias == query.operation.target)
        if not target.identifier or not target.identifier.strip():
            return "What is your reservation ID?"
        reservation_id = target.identifier
        record = self.store.get_reservation(reservation_id)
        self.events.append({"kind": "lookup", "reservation_id": reservation_id, "result": record})
        if query.operation.action == "lookup_reservation":
            return self.finish("lookup_answered", str(record))

        evaluation = evaluate_policy(context, record, authorized)
        self.events.append({"kind": "evaluation", "reservation_id": reservation_id, "result": evaluation})
        if not evaluation["eligible"]:
            failures = []
            for result in evaluation["conditions"]:
                if result["category"] != "eligibility" or result["passed"]:
                    continue
                node = result["condition"]
                if node == "Exists":
                    failures = [f"Reservation {reservation_id} was not found."]
                    break
                if result["error"]:
                    failures.append(f"Cannot verify {node}: {result['error']}.")
                elif node == "IsRefundable":
                    failures.append("This reservation is not refundable.")
                elif node == "IsConfirmed":
                    failures.append(f"Reservation status is {record.get('status')}; it must be confirmed.")
                elif node == "DepartureBeyond24Hours":
                    rule = next(c.properties for c in context.conditions if c.node_id == node)
                    failures.append(f"Departure is {record.get('hours_until_departure')} hours away; "
                                    f"cancellation requires more than {rule['value']} hours.")
                else:
                    failures.append(f"Requirement not satisfied: {result['description']}.")
            return self.finish("ineligible", "Cancellation is not eligible. " + " ".join(failures))
        if query.operation.action == "check_cancellation_eligibility":
            return self.finish("eligibility_answered", f"Reservation {reservation_id} is eligible for cancellation. Explicit confirmation is required before cancelling.")
        if not evaluation["execution_allowed"]:
            self.pending = (query, reservation_id)
            return f"Please confirm cancellation of {reservation_id}. Reply 'yes' or 'confirm {reservation_id}', or 'no' to decline."

        # Fresh facts and policy at the mutation boundary.
        context = self.retrieve(query, self.graph)
        current = self.store.get_reservation(reservation_id)
        guard = evaluate_policy(context, current, authorized)
        if not guard["execution_allowed"]:
            return self.finish("blocked", "Cancellation was blocked because its requirements are no longer satisfied.")
        result = self.store.cancel_reservation(reservation_id)
        self.events.append({"kind": "cancel", "reservation_id": reservation_id,
                            "guard": guard, "result": result})
        if result.get("success"):
            return self.finish("cancelled", f"Reservation {reservation_id} was cancelled successfully.")
        return self.finish("cancellation_failed", f"Cancellation failed: {result.get('error', 'unknown error')}")

    def report(self, expected=None):
        final = self.store.snapshot()
        expected_state = deepcopy(self.initial)
        if expected is not None:
            for identifier, properties in expected.items():
                if identifier not in expected_state:
                    raise ValueError(f"Unknown expected reservation: {identifier}")
                expected_state[identifier].update(properties)
        cancellations = [e for e in self.events if e["kind"] == "cancel"]
        return {
            "initial": self.initial, "final": final,
            "outcome": self.outcome,
            "state_success": final == expected_state if expected is not None else None,
            "cancellation_attempts": len(cancellations),
            "cancellation_guards_passed": all(e["guard"]["execution_allowed"]
                for e in cancellations) if cancellations else None,
            "transcript": list(self.transcript), "events": list(self.events),
        }


def run_customer_session(session, mode="manual", client=None, user_model=None,
                         goal="Cancel ABC123 and confirm when asked.", max_turns=8,
                         expected=None, input_fn=input):
    if mode not in {"manual", "simulated"}:
        raise ValueError("mode must be manual or simulated")
    if max_turns <= 0:
        raise ValueError("max_turns must be positive")
    if mode == "simulated" and (client is None or user_model is None):
        raise ValueError("Simulated mode requires a client and user model")
    history = [{"role": "system", "content": CUSTOMER_PROMPT.format(goal=goal)}]
    termination, error = "max_turns", None
    try:
        for _ in range(max_turns):
            if mode == "simulated":
                print(f"Waiting for simulated customer ({user_model})...", flush=True)
                customer_turn = simulated_customer_turn(client, user_model, history)
                message = customer_turn.message
                session.events.append({"kind": "customer_turn", **customer_turn.model_dump()})
                pending_confirmation = (
                    session.pending is not None
                    and is_confirmation(message, session.pending[1])
                )
                pending_decline = session.pending is not None and message.strip().lower().rstrip('.!') in {
                    "no", "no thanks", "do not cancel", "don't cancel"
                }
                if customer_turn.done and (pending_confirmation or pending_decline):
                    # A simulator completion flag cannot discard a valid answer
                    # to the controller's outstanding confirmation request.
                    session.events.append({
                        "kind": "simulator_protocol_correction",
                        "reason": "Pending confirmation response must be processed before completion",
                        "original_done": True,
                    })
                    customer_turn = customer_turn.model_copy(update={"done": False})
                if customer_turn.done:
                    print(f"Customer: {message}", flush=True)
                    session.transcript.append({"role": "user", "content": message})
                    termination = "customer_stop"
                    break
            else:
                message = input_fn("You (/quit to stop): ")
            if mode == "manual" and message.strip() == "/quit":
                termination = "customer_stop"
                break
            print(f"Customer: {message}", flush=True)
            history.append({"role": "assistant", "content": customer_turn.model_dump_json()
                            if mode == "simulated" else message})
            print("Processing customer message...", flush=True)
            answer = session.reply(message)
            print(f"Agent: {answer}", flush=True)
            history.append({"role": "user", "content": answer})
            if session.completed:
                termination = "task_completed"
                break
    except Exception as exc:
        termination, error = "error", f"{type(exc).__name__}: {exc}"
    report = session.report(expected)
    report.update(termination=termination, error=error)
    if error and expected is not None:
        report["state_success"] = False
    return report
