import unittest

from agent.evidence import EvidenceStore
from agent.evaluation import evaluate, validate_tasks


class EvidenceTests(unittest.TestCase):
    def test_deduplication_and_undelivered_citations(self):
        evidence = EvidenceStore()
        first = evidence.add({"path": "main.cpp", "kind": "source"})
        self.assertEqual(first, evidence.add({"kind": "source", "path": "main.cpp"}))
        audit = evidence.audit("Answer [ev-1] [ev-9]", {"ev-1"})
        self.assertEqual(audit["invalid_citations"], ["ev-9"])
        self.assertFalse(evidence.audit("Answer [ev-1]", set())["citations_valid"])
        self.assertFalse(evidence.audit("Answer", {"ev-1"})["has_citations"])

    def test_evaluation_does_not_claim_semantic_correctness(self):
        task = {"id": "test", "question": "question", "review_rubric": "Review behavior",
                "answer_contains": ["42"], "expected_evidence_paths": ["helpers.cpp"]}
        validate_tasks([task])
        evidence = EvidenceStore()
        evidence.add({"path": "helpers.cpp"})
        report = {"answer": "Returns 42 [ev-1]", "citation_audit": evidence.audit("Returns 42 [ev-1]", {"ev-1"}),
                  "evidence": evidence.records, "completion": "answered", "mode": "graph", "usage": [],
                  "tool_events": [], "elapsed_ms": 1}
        evaluation = evaluate(task, report)
        self.assertTrue(evaluation["checks_passed"])
        self.assertIsNone(evaluation["semantic_correctness"])
        self.assertTrue(evaluation["review_required"])
        report["answer"] = "Returns 7 [ev-1]"
        self.assertFalse(evaluate(task, report)["checks_passed"])
