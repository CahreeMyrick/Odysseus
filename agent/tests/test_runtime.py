from dataclasses import replace
import json
import subprocess
import sys
import unittest
from contextlib import redirect_stdout
from io import StringIO
from unittest.mock import patch

from agent.runtime import InspectionAgent
from .support import Workspace


def response(content="", calls=None):
    message = {"role": "assistant", "content": content}
    if calls:
        message["tool_calls"] = [{"function": {"name": name, "arguments": args}} for name, args in calls]
    return {"message": message, "usage": {"input_tokens": 10, "output_tokens": 5}}


class ScriptedClient:
    def __init__(self, responses):
        self.responses = iter(responses)

    def complete(self, model, messages, tools):
        return next(self.responses)


class RuntimeTests(unittest.TestCase):
    def setUp(self):
        self.workspace = Workspace()
        self.addCleanup(self.workspace.close)

    def test_full_tool_loop_and_citation_trace(self):
        tools = self.workspace.tools()
        client = ScriptedClient([
            response(calls=[("read_file", {"path": "helpers.cpp"})]),
            response("helper returns 42 [ev-1]"),
        ])
        agent = InspectionAgent(client, tools, self.workspace.config)
        report = agent.ask("What does helper return?")
        self.assertTrue(report["citation_audit"]["citations_valid"])
        self.assertEqual(report["tool_events"][0]["result"]["status"], "ok")
        self.assertEqual(len(report["usage"]), 2)
        self.assertIn("helpers.cpp", report["references"][0])

    def test_invalid_tool_and_fabricated_citation(self):
        client = ScriptedClient([response(calls=[("delete_file", {"path": "helpers.cpp"})]),
                                 response("Deleted it [ev-99]")])
        report = InspectionAgent(client, self.workspace.tools(), self.workspace.config).ask("Question")
        self.assertEqual(report["tool_events"][0]["result"]["status"], "error")
        self.assertEqual(report["citation_audit"]["invalid_citations"], ["ev-99"])
        self.assertTrue((self.workspace.repo / "helpers.cpp").exists())

    def test_round_budget_and_invalid_arguments(self):
        client = ScriptedClient([response(calls=[("read_file", {"wrong": "helpers.cpp"})])])
        config = replace(self.workspace.config, max_tool_rounds=1)
        report = InspectionAgent(client, self.workspace.tools(), config).ask("Question")
        self.assertEqual(report["completion"], "tool_limit")
        self.assertEqual(report["tool_events"][0]["result"]["status"], "error")

    def test_cli_without_model_and_trace_overwrite(self):
        command = [sys.executable, "-m", "agent", "--repo", str(self.workspace.repo), "--graph",
                   str(self.workspace.path), "--inspect-only"]
        result = subprocess.run(command, capture_output=True, text=True, check=True)
        status = json.loads(result.stdout)
        self.assertTrue(status["data"]["revision_matches"])
        trace = self.workspace.root / "trace.json"
        trace.write_text("keep")
        rejected = subprocess.run(command + ["--trace", str(trace)], capture_output=True, text=True)
        self.assertEqual(rejected.returncode, 2)
        self.assertEqual(trace.read_text(), "keep")

    def test_benchmark_compares_fresh_sessions_and_writes_trace(self):
        from agent.__main__ import main
        tasks = self.workspace.root / "tasks.json"
        tasks.write_text(json.dumps([{"id": "helper", "question": "What does helper return?",
            "answer_contains": ["42"], "expected_evidence_paths": ["helpers.cpp"],
            "review_rubric": "Check implementation."}]))
        trace = self.workspace.root / "comparison.json"
        client = ScriptedClient([
            response(calls=[("read_file", {"path": "helpers.cpp"})]), response("42 [ev-1]"),
            response(calls=[("read_file", {"path": "helpers.cpp"})]), response("42 [ev-1]"),
        ])
        with patch("agent.__main__.OllamaClient", return_value=client), redirect_stdout(StringIO()):
            status = main(["--repo", str(self.workspace.repo), "--graph", str(self.workspace.path),
                           "--benchmark", str(tasks), "--compare", "--trace", str(trace)])
        self.assertEqual(status, 0)
        reports = json.loads(trace.read_text())["reports"]
        self.assertEqual([r["mode"] for r in reports], ["source-only", "graph"])
        self.assertTrue(all(r["evaluation"]["checks_passed"] for r in reports))
        self.assertTrue(all(len(r["evidence"]) == 1 for r in reports))
