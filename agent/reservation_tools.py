"""Model-callable reservation operations and their evaluation events."""

import json

from evaluator import ToolEvent
from tools import ReservationStore


class ReservationTools:
    """Bind episode state while exposing only reservation IDs to the model."""

    def __init__(
        self,
        store: ReservationStore,
        tool_events: list[ToolEvent],
        authorized_ids: set[str],
    ) -> None:
        self.store = store
        self.tool_events = tool_events
        self.authorized_ids = authorized_ids

    def get_reservation(self, reservation_id: str) -> str:
        """Get current reservation data by ID.

        Args:
            reservation_id: Reservation to retrieve.
        """
        result = self.store.get_reservation(reservation_id)
        self.tool_events.append(ToolEvent("get_reservation", reservation_id, result))
        return json.dumps(result)

    def cancel_reservation(self, reservation_id: str) -> str:
        """Attempt to cancel a reservation by ID. Requires policy compliance.

        Args:
            reservation_id: Reservation to cancel.
        """
        confirmed = reservation_id in self.authorized_ids
        result = self.store.cancel_reservation(reservation_id)
        self.tool_events.append(ToolEvent("cancel_reservation", reservation_id, result, confirmed))
        return json.dumps(result)
