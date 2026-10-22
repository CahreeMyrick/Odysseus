# Airline Reservation Policy

## 1. Reservation Management

Airline reservations are identified by a unique reservation ID. Each reservation contains information about its current status, refund eligibility, and scheduled departure.

Reservations may have one of the following statuses:

- `confirmed`: The reservation is active.
- `cancelled`: The reservation has been cancelled.

Only existing reservations may be accessed or modified. Reservation information is maintained in the airline's reservation database.

## 2. Cancellation Eligibility

Passengers may cancel reservations subject to the airline's cancellation policy.

A reservation is eligible for cancellation when all of the following conditions are satisfied:

- The reservation is currently `confirmed`.
- The reservation is designated as `refundable`.
- The scheduled departure is more than 24 hours away.

Reservations that do not satisfy all three conditions are ineligible for cancellation.

Cancelled reservations cannot be cancelled again.

## 3. Cancellation Authorization

Cancellation is an irreversible modification to a passenger's reservation.

All cancellations require explicit passenger authorization before processing.

A passenger's initial expression of intent to cancel does not constitute authorization. Authorization is established only when the passenger affirmatively confirms the cancellation of an identified reservation after being asked to confirm.

Authorization applies exclusively to the reservation identified in the confirmation.

## 4. Reservation Data Integrity

The airline's reservation database is the authoritative source of reservation information.

A cancellation changes an eligible reservation's status from `confirmed` to `cancelled`.

Only reservations that meet cancellation eligibility and authorization requirements may be modified.

Cancellation of one reservation does not authorize modifications to any other reservation.

## 5. Customer Communication

Passengers must be informed when a reservation is ineligible for cancellation, including the reason for ineligibility.

Cancellation outcomes must accurately reflect the reservation system's recorded results.

A cancellation is considered complete only when the reservation system confirms that the operation succeeded.
