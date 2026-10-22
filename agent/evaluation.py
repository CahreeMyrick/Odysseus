"""Deterministic benchmark checks. Semantic correctness remains a review task."""


def evaluate(task: dict, report: dict) -> dict:
    answer = report["answer"].casefold()
    required_terms = task.get("answer_contains", [])
    missing_terms = [term for term in required_terms if term.casefold() not in answer]
    audit = report["citation_audit"]
    cited = [report["evidence"][eid] for eid in audit["citations"] if eid in report["evidence"]]
    paths = {record.get("path") for record in cited}
    missing_paths = sorted(set(task.get("expected_evidence_paths", [])) - paths)
    retrieval_errors = sum(event["result"]["status"] == "error" for event in report["tool_events"])
    usage = report.get("usage", [])
    input_counts = [item.get("input_tokens") for item in usage]
    output_counts = [item.get("output_tokens") for item in usage]
    return {"task_id": task["id"], "mode": report["mode"],
            "completion": report["completion"], "missing_answer_terms": missing_terms,
            "missing_cited_paths": missing_paths, "citations_valid": audit["citations_valid"],
            "invalid_citations": audit["invalid_citations"],
            "checks_passed": report["completion"] == "answered" and not missing_terms and not missing_paths and audit["citations_valid"],
            "semantic_correctness": None, "semantic_support": None,
            "review_required": True,
            "review_rubric": task.get("review_rubric", "Check correctness, evidence support and limitations."),
            "tool_calls": len(report["tool_events"]), "tool_errors": retrieval_errors,
            "input_tokens": sum(input_counts) if input_counts and all(type(n) is int for n in input_counts) else None,
            "output_tokens": sum(output_counts) if output_counts and all(type(n) is int for n in output_counts) else None,
            "elapsed_ms": report["elapsed_ms"]}


def validate_tasks(tasks: object) -> list[dict]:
    if not isinstance(tasks, list) or not tasks:
        raise ValueError("Benchmark must be a nonempty array of tasks")
    ids = set()
    for task in tasks:
        if not isinstance(task, dict):
            raise ValueError("Task must be an object")
        for field in ("id", "question", "review_rubric"):
            if not isinstance(task.get(field), str) or not task[field].strip():
                raise ValueError(f"Task needs a nonempty {field}")
        if task["id"] in ids:
            raise ValueError("Duplicate task ID")
        ids.add(task["id"])
        for field in ("answer_contains", "expected_evidence_paths"):
            if not isinstance(task.get(field, []), list) or not all(isinstance(v, str) and v for v in task.get(field, [])):
                raise ValueError(f"{field} must be an array of nonempty strings")
    return tasks
