"""Serializable observations for inspecting and evaluating agent behavior."""
from dataclasses import asdict, dataclass
import json
from pathlib import Path


@dataclass
class ToolEvent:
    name: str
    arguments: dict
    result: dict
    elapsed_ms: float

    def to_dict(self):
        return asdict(self)


def save_report(path: Path, report: dict) -> None:
    # Traces can contain repository source. Save only when explicitly requested.
    with Path(path).open("x", encoding="utf-8") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
