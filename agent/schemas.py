"""Tool result contracts, independent of the model provider."""
import json
from typing import Any


STATUSES = {"ok", "not_found", "ambiguous", "unsupported", "stale", "error"}


def result(status: str = "ok", *, data: Any = None, evidence_ids: list[str] | None = None,
           revision: str | None = None, truncated: bool = False,
           limitations: list[str] | None = None) -> dict:
    if status not in STATUSES:
        raise ValueError("Unknown result status")
    return {"status": status, "revision": revision, "data": data,
            "evidence_ids": evidence_ids or [], "truncated": truncated,
            "limitations": limitations or []}


def encode(value: dict) -> str:
    return json.dumps(value, ensure_ascii=False)


def bounded_int(value: int, name: str, low: int, high: int) -> int:
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f"{name} must be an integer from {low} to {high}")
    return value


def text(value: str, name: str, *, allow_empty: bool = False) -> str:
    if not isinstance(value, str) or (not allow_empty and not value.strip()):
        raise ValueError(f"{name} must be {'a' if allow_empty else 'a nonempty'} string")
    return value
