import copy
import unittest

from agent.graph_store import GraphStore
from .support import Workspace, make_graph


class GraphStoreTests(unittest.TestCase):
    def setUp(self):
        self.workspace = Workspace()
        self.addCleanup(self.workspace.close)

    def test_indexes_and_ownership(self):
        store = GraphStore(self.workspace.path)
        self.assertEqual(len(store.names["helper"]), 2)
        self.assertEqual(store.describe("call:helper")["path"], "main.cpp")
        self.assertFalse(store.status()["capabilities"]["resolved_calls"])
        self.assertEqual(store.status()["files"], 3)

    def test_invalid_graphs_rejected(self):
        for mutation in ("duplicate", "dangling", "relation", "provenance", "unsafe", "version", "cross_file"):
            with self.subTest(mutation=mutation):
                data = make_graph(self.workspace.revision)
                if mutation == "duplicate":
                    data["nodes"].append(copy.deepcopy(data["nodes"][0]))
                elif mutation == "dangling":
                    data["edges"][0]["target"] = "missing"
                elif mutation == "relation":
                    data["edges"][0]["type"] = "CALLS"
                elif mutation == "provenance":
                    data["nodes"][2]["properties"]["provenance"]["snapshot_id"] = "wrong"
                elif mutation == "unsafe":
                    data["nodes"][2]["properties"]["path"] = "../outside"
                elif mutation == "version":
                    data["schema_version"] = True
                else:
                    data["edges"][-1]["source"] = "function:helpers.cpp"
                self.workspace.write_graph(data)
                with self.assertRaises(ValueError):
                    GraphStore(self.workspace.path)

    def test_revision_selection_is_explicit(self):
        data = make_graph(self.workspace.revision)
        data["nodes"].append({"id": "revision-2", "type": "Revision", "properties": {"commit": "other"}})
        data["edges"].append({"source": "repo", "type": "HAS_REVISION", "target": "revision-2", "properties": {}})
        self.workspace.write_graph(data)
        with self.assertRaisesRegex(ValueError, "Select exactly one"):
            GraphStore(self.workspace.path)
        store = GraphStore(self.workspace.path, self.workspace.revision)
        self.assertNotIn("revision-2", store.graph)
