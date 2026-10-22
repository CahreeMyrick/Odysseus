"""Session evidence and mechanical citation checks, not semantic entailment."""
import json
import re


class EvidenceStore:
    def __init__(self):
        self.records: dict[str, dict] = {}
        self._identities: dict[str, str] = {}

    def add(self, record: dict) -> str:
        identity = json.dumps(record, sort_keys=True, ensure_ascii=False)
        if identity not in self._identities:
            eid = f"ev-{len(self.records) + 1}"
            self._identities[identity] = eid
            self.records[eid] = record
        return self._identities[identity]

    def audit(self, answer: str, delivered: set[str]) -> dict:
        citations = list(dict.fromkeys(re.findall(r"\[(ev-\d+)\]", answer)))
        invalid = [eid for eid in citations if eid not in self.records or eid not in delivered]
        return {"citations": citations, "invalid_citations": invalid,
                "has_citations": bool(citations), "citations_valid": bool(citations) and not invalid,
                "semantic_support_checked": False}

    def references(self, citations: list[str]) -> list[str]:
        lines = []
        for eid in citations:
            if eid not in self.records:
                continue
            record = self.records[eid]
            location = record.get("path") or record.get("node_id") or "graph relationship"
            if record.get("start_line"):
                location += f":{record['start_line']}-{record['end_line']}"
            revision = record.get("revision") or "unknown revision"
            suffix = f" working-tree sha256={record['sha256'][:12]}" if record.get("sha256") else " graph snapshot"
            lines.append(f"[{eid}] {location} ({revision}{suffix})")
        return lines
