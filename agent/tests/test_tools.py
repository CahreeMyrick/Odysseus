from dataclasses import replace
import unittest

from .support import Workspace


class ToolsTests(unittest.TestCase):
    def setUp(self):
        self.workspace = Workspace()
        self.addCleanup(self.workspace.close)
        self.tools = self.workspace.tools()

    def test_matching_symbol_source_and_dirty_file(self):
        output = self.tools.execute("read_symbol_source", {"node_id": "function:helpers.cpp"})
        self.assertEqual(output["status"], "ok")
        self.assertIn("return 42;", output["data"]["text"])
        self.assertTrue(output["evidence_ids"])
        (self.workspace.repo / "helpers.cpp").write_text("int helper() { return 99; }\n")
        stale = self.tools.execute("read_symbol_source", {"node_id": "function:helpers.cpp"})
        self.assertEqual(stale["status"], "stale")
        current = self.tools.execute("read_file", {"path": "helpers.cpp"})
        self.assertIn("99", current["data"]["text"])
        self.assertNotEqual(output["data"]["sha256"], current["data"]["sha256"])

    def test_paths_limits_and_unsupported_queries(self):
        (self.workspace.root / "secret.txt").write_text("outside")
        (self.workspace.repo / "escape.txt").symlink_to(self.workspace.root / "secret.txt")
        for path in ("../secret.txt", "escape.txt", ".git/config", str(self.workspace.root / "secret.txt")):
            with self.subTest(path=path):
                self.assertEqual(self.tools.execute("read_file", {"path": path})["status"], "error")
        self.assertEqual(self.tools.execute("read_file", {"path": "main.cpp", "end_line": 500})["status"], "error")
        self.assertEqual(self.tools.execute("read_file", {"path": "main.cpp", "start_line": True})["status"], "error")
        self.assertEqual(self.tools.execute("find_path", {"source_id": "function:main.cpp", "target_id": "function:helpers.cpp", "relations": ["CALLS"]})["status"], "unsupported")
        self.assertEqual(self.tools.execute("unknown", {})["status"], "error")

    def test_search_limits_and_current_evidence(self):
        result = self.tools.execute("search_source", {"query": "helper", "limit": 1})
        self.assertEqual(result["status"], "ok")
        self.assertTrue(result["truncated"])
        self.assertEqual(len(result["evidence_ids"]), 1)
        limited = self.workspace.tools(config=replace(self.workspace.config, max_search_files=1))
        result = limited.execute("search_source", {"query": "not present"})
        self.assertEqual(result["status"], "not_found")
        self.assertTrue(result["truncated"])

    def test_baseline_has_no_graph_tools_or_metadata(self):
        baseline = self.workspace.tools(graph=False)
        self.assertNotIn("find_symbols", baseline.available())
        status = baseline.execute("repository_status", {})
        self.assertNotIn("graph", status["data"])
        self.assertIsNone(status["data"]["graph_revision"])
        self.assertEqual(baseline.execute("find_symbols", {"query": "helper"})["status"], "error")

    def test_oversized_response_delivers_no_evidence(self):
        tools = self.workspace.tools(config=replace(self.workspace.config, max_result_chars=20))
        result = tools.execute("read_file", {"path": "main.cpp"})
        self.assertTrue(result["truncated"])
        self.assertEqual(result["evidence_ids"], [])

    def test_ollama_can_generate_all_tool_schemas(self):
        from ollama._utils import convert_function_to_tool
        for tool in self.tools.available().values():
            schema = convert_function_to_tool(tool)
            self.assertEqual(schema.function.name, tool.__name__)
