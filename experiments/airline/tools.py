"""Tiny in-memory reservation store for controlled airline-agent experiments."""

from dataclasses import dataclass


@dataclass
class Reservation:
    reservation_id: str
    status: str
    refundable: bool
    hours_until_departure: float


class ReservationStore:
    def __init__(self, reservations: list[Reservation]) -> None:
        self._records = {r.reservation_id: r for r in reservations}

    def get_reservation(self, reservation_id: str) -> dict:
        """Look up a reservation by its reservation ID."""
        record = self._records.get(reservation_id)
        if record is None:
            return {"success": False, "error": "Reservation not found"}
        return {"success": True, **vars(record)}

    def cancel_reservation(self, reservation_id: str) -> dict:
        """Attempt cancellation; returns system outcome, not an authorization decision."""
        record = self._records.get(reservation_id)
        if record is None:
            return {"success": False, "error": "Reservation not found"}
        if record.status != "confirmed":
            return {"success": False, "error": "Reservation is not confirmed"}
        if not record.refundable:
            return {"success": False, "error": "Reservation is not refundable"}
        if record.hours_until_departure <= 24:
            return {"success": False, "error": "Departure is within 24 hours"}
        record.status = "cancelled"
        return {"success": True, "reservation_id": reservation_id, "status": record.status}

    def snapshot(self) -> dict[str, dict]:
        """Return independent copies suitable for before/after evaluation."""
        return {key: vars(record).copy() for key, record in self._records.items()}

