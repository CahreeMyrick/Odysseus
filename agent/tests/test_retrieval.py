import unittest

from agent import retrieval
from .support import Workspace


class RetrievalTests(unittest.TestCase):
    def setUp(self):
        self.workspace = Workspace()
        self.addCleanup(self.workspace.close)
        self.store = self.workspace.tools().graph

    def test_name_disambiguation_and_path_filter(self):
        result = retrieval.find_symbols(self.store, "helper", "", 10)
        self.assertTrue(result["ambiguous"])
        exact = retrieval.find_symbols(self.store, "other::helper", "", 10)
        self.assertEqual(exact["matches"][0]["id"], "function:other.cpp")
        self.assertFalse(exact["ambiguous"])
        scoped = retrieval.find_symbols(self.store, "helper", "helpers.cpp", 10)
        self.assertEqual(len(scoped["matches"]), 2)  # matching file and its function
        self.assertFalse(scoped["ambiguous"])

    def test_bfs_is_bounded_and_preserves_edge_direction(self):
        one = retrieval.neighborhood(self.store, "file:main.cpp", {"DEFINES"}, "out", 1, 10)
        self.assertEqual([edge["type"] for edge in one["edges"]], ["DEFINES"])
        incoming = retrieval.neighborhood(self.store, "function:main.cpp", {"DEFINES"}, "in", 1, 10)
        self.assertEqual(incoming["edges"][0]["source"], "file:main.cpp")
        limited = retrieval.neighborhood(self.store, "revision", set(retrieval.RELATIONS), "out", 3, 1)
        self.assertTrue(limited["truncated"])
        self.assertEqual(len(limited["edges"]), 1)

    def test_directed_path_and_unsupported_resolution(self):
        allowed = {"CONTAINS", "DEFINES", "HAS_CALL_SITE"}
        path = retrieval.shortest_path(self.store, "revision", "function:main.cpp", allowed, 3)
        self.assertTrue(path["found"])
        self.assertEqual(len(path["edges"]), 2)
        reverse = retrieval.shortest_path(self.store, "function:main.cpp", "revision", allowed, 3)
        self.assertFalse(reverse["found"])
        short = retrieval.shortest_path(self.store, "revision", "function:main.cpp", allowed, 1)
        self.assertFalse(short["found"])
        same = retrieval.shortest_path(self.store, "revision", "revision", allowed, 1)
        self.assertTrue(same["found"])
        with self.assertRaises(NotImplementedError):
            retrieval.relations(["CALLS"])
        with self.assertRaises(ValueError):
            retrieval.relations([], required=True)
