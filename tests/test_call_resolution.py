import copy
import unittest

from odysseus_agent import retrieval
from odysseus_agent.graph_store import GraphStore

from .support import Workspace, make_graph


def inferred_graph(revision):
    data = make_graph(revision)
    data["schema_version"] = 2
    for edge in data["edges"]:
        edge["properties"] = {"evidence_kind": "EXTRACTED", "method": "syntax"}
    data["edges"].append(
        {
            "source": "call:helper",
            "type": "INFERRED_TARGET",
            "target": "function:helpers.cpp",
            "properties": {
                "evidence_kind": "INFERRED",
                "resolution_status": "inferred",
                "method": "scope_and_relative_include_name_match",
                "resolver_version": "odysseus-call-candidates-v1",
                "matched_name": "helper",
                "evidence": ["call:helper", "file:main.cpp", "function:helpers.cpp"],
                "limitations": "Candidate only; no binding or overload checking",
            },
        }
    )
    return data


class CallResolutionTests(unittest.TestCase):
    def setUp(self):
        self.workspace = Workspace()
        self.addCleanup(self.workspace.close)
        self.data = inferred_graph(self.workspace.revision)
        self.workspace.write_graph(self.data)

    def test_inference_is_opt_in_and_evidence_survives_traversal(self):
        tools = self.workspace.tools()
        store = tools.graph
        self.assertTrue(store.status()["capabilities"]["inferred_call_targets"])
        self.assertFalse(store.status()["capabilities"]["resolved_calls"])
        self.assertNotIn("INFERRED_TARGET", retrieval.relations([]))
        path = retrieval.shortest_path(
            store,
            "function:main.cpp",
            "function:helpers.cpp",
            {"HAS_CALL_SITE", "INFERRED_TARGET"},
            2,
        )
        self.assertTrue(path["found"])
        self.assertEqual(path["edges"][-1]["properties"]["evidence_kind"], "INFERRED")
        result = tools._graph_result(path)
        self.assertTrue(any("candidate" in note for note in result["limitations"]))
        self.assertTrue(result["evidence_ids"])

    def test_invalid_inference_is_rejected(self):
        for field, value in (
            ("evidence_kind", "EXTRACTED"),
            ("resolution_status", "resolved"),
            ("resolver_version", ""),
            ("evidence", ["missing"]),
            ("evidence", ["call:helper"]),
        ):
            with self.subTest(field=field):
                data = copy.deepcopy(self.data)
                data["edges"][-1]["properties"][field] = value
                self.workspace.write_graph(data)
                with self.assertRaises(ValueError):
                    GraphStore(self.workspace.path)

    def test_cross_revision_target_is_rejected_before_selection(self):
        data = copy.deepcopy(self.data)
        data["nodes"].append({"id": "old", "type": "Revision", "properties": {"commit": "old"}})
        data["edges"].append(
            {
                "source": "repo",
                "type": "HAS_REVISION",
                "target": "old",
                "properties": {"evidence_kind": "EXTRACTED", "method": "syntax"},
            }
        )
        for edge in data["edges"]:
            if edge["source"] == "revision" and edge["target"] == "file:helpers.cpp":
                edge["source"] = "old"
        self.workspace.write_graph(data)
        with self.assertRaisesRegex(ValueError, "one revision"):
            GraphStore(self.workspace.path, self.workspace.revision)

    def test_v1_remains_readable_but_cannot_claim_inference(self):
        self.data["schema_version"] = 1
        self.workspace.write_graph(self.data)
        with self.assertRaises(ValueError):
            GraphStore(self.workspace.path)
        self.workspace.write_graph(make_graph(self.workspace.revision))
        self.assertFalse(
            GraphStore(self.workspace.path).status()["capabilities"]["inferred_call_targets"]
        )

    def explicit_graph(self):
        data = copy.deepcopy(self.data)
        data["schema_version"] = 3
        edge = data["edges"][-1]
        edge["type"] = "EXTRACTED_TARGET"
        edge["properties"].update(
            evidence_kind="EXTRACTED",
            resolution_status="extracted",
            method="explicit_qualified_name",
            resolver_version="odysseus-call-targets-v2",
            limitations="Explicit syntax association, not compiler verification",
        )
        return data

    def test_extracted_targets_preserve_evidence_and_remain_opt_in(self):
        self.workspace.write_graph(self.explicit_graph())
        tools = self.workspace.tools()
        store = tools.graph
        self.assertTrue(store.status()["capabilities"]["extracted_call_targets"])
        self.assertFalse(store.status()["capabilities"]["resolved_calls"])
        self.assertNotIn("EXTRACTED_TARGET", retrieval.relations([]))
        path = retrieval.shortest_path(
            store,
            "function:main.cpp",
            "function:helpers.cpp",
            {"HAS_CALL_SITE", "EXTRACTED_TARGET"},
            2,
        )
        self.assertTrue(path["found"])
        self.assertEqual(path["edges"][-1]["properties"]["evidence_kind"], "EXTRACTED")
        result = tools._graph_result(path)
        self.assertTrue(any("compiler" in note for note in result["limitations"]))

    def test_explicit_contract_rejects_wrong_method_status_version_and_ambiguity(self):
        for mutation in (
            "method",
            "status",
            "kind",
            "version",
            "multiple",
            "this_evidence",
            "evidence",
        ):
            with self.subTest(mutation=mutation):
                data = self.explicit_graph()
                edge = data["edges"][-1]
                if mutation == "method":
                    edge["properties"]["method"] = "name_guess"
                elif mutation == "status":
                    edge["properties"]["resolution_status"] = "ambiguous"
                elif mutation == "kind":
                    edge["properties"]["evidence_kind"] = "INFERRED"
                elif mutation == "version":
                    data["schema_version"] = 2
                elif mutation == "this_evidence":
                    edge["properties"]["method"] = "explicit_this_receiver"
                elif mutation == "evidence":
                    edge["properties"]["evidence"] = ["missing"]
                else:
                    data["edges"].append(copy.deepcopy(self.data["edges"][-1]))
                self.workspace.write_graph(data)
                with self.assertRaises(ValueError):
                    GraphStore(self.workspace.path)

    def test_extracted_targets_cannot_cross_revisions(self):
        self.data = self.explicit_graph()
        self.test_cross_revision_target_is_rejected_before_selection()

    def declaration_graph(self):
        data = copy.deepcopy(self.data)
        data["schema_version"] = 4
        definition = next(n for n in data["nodes"] if n["id"] == "function:helpers.cpp")
        definition["properties"].update(kind="function", signature_key="(int)")
        declaration = copy.deepcopy(definition)
        declaration["id"] = "declaration:helper"
        declaration["properties"]["kind"] = "declaration"
        data["nodes"].append(declaration)
        data["edges"].extend(
            [
                {
                    "source": "file:main.cpp",
                    "target": declaration["id"],
                    "type": "DECLARES",
                    "properties": {"evidence_kind": "EXTRACTED", "method": "syntax"},
                },
                {
                    "source": declaration["id"],
                    "target": definition["id"],
                    "type": "DECLARATION_OF",
                    "properties": {
                        "evidence_kind": "EXTRACTED",
                        "method": "exact_sibling_signature",
                        "resolver_version": "odysseus-call-targets-v3",
                        "evidence": [declaration["id"], definition["id"]],
                        "limitations": "Syntactic signature association",
                    },
                },
            ]
        )
        return data

    def test_declaration_links_preserve_both_locations(self):
        self.workspace.write_graph(self.declaration_graph())
        store = GraphStore(self.workspace.path)
        path = retrieval.shortest_path(
            store, "declaration:helper", "function:helpers.cpp", {"DECLARATION_OF"}, 1
        )
        self.assertTrue(path["found"])
        self.assertEqual([n["path"] for n in path["nodes"]], ["main.cpp", "helpers.cpp"])
        self.assertTrue(store.status()["capabilities"]["declaration_linking"])
        self.assertNotIn("DECLARATION_OF", retrieval.relations([]))

    def test_invalid_declaration_links_are_rejected(self):
        for mutation in ("signature", "kind", "evidence", "version"):
            with self.subTest(mutation=mutation):
                data = self.declaration_graph()
                if mutation == "signature":
                    data["nodes"][-1]["properties"]["signature_key"] = "(double)"
                elif mutation == "kind":
                    data["nodes"][-1]["properties"]["kind"] = "function"
                elif mutation == "evidence":
                    data["edges"][-1]["properties"]["evidence"] = ["missing"]
                else:
                    data["schema_version"] = 3
                self.workspace.write_graph(data)
                with self.assertRaises(ValueError):
                    GraphStore(self.workspace.path)

    def include_graph(self):
        data = copy.deepcopy(self.data)
        data["schema_version"] = 4
        data["edges"].append(
            {
                "source": "file:main.cpp",
                "target": "file:helpers.cpp",
                "type": "INCLUDES",
                "properties": {
                    "evidence_kind": "INFERRED",
                    "method": "unique_include_suffix",
                    "include_kind": "quoted",
                    "raw_include": "helpers.cpp",
                    "resolver_version": "test",
                    "limitations": "Search path unknown",
                    "span": {"start_line": 1, "end_line": 1},
                    "evidence": ["file:main.cpp", "file:helpers.cpp"],
                },
            }
        )
        return data

    def test_includes_do_not_change_file_ownership(self):
        self.workspace.write_graph(self.include_graph())
        store = GraphStore(self.workspace.path)
        self.assertEqual(store.status()["files"], 3)
        self.assertEqual(store.describe("function:helpers.cpp")["path"], "helpers.cpp")
        self.assertNotIn("INCLUDES", retrieval.relations([]))
        path = retrieval.shortest_path(store, "file:main.cpp", "file:helpers.cpp", {"INCLUDES"}, 1)
        self.assertTrue(path["found"])

    def test_include_evidence_and_revision_are_validated(self):
        for mutation in ("label", "evidence", "revision"):
            with self.subTest(mutation=mutation):
                data = self.include_graph()
                if mutation == "label":
                    data["edges"][-1]["properties"]["evidence_kind"] = "EXTRACTED"
                elif mutation == "evidence":
                    data["edges"][-1]["properties"]["evidence"] = ["missing"]
                else:
                    data["edges"] = [
                        e
                        for e in data["edges"]
                        if not (e["source"] == "revision" and e["target"] == "file:helpers.cpp")
                    ]
                self.workspace.write_graph(data)
                with self.assertRaises(ValueError):
                    GraphStore(self.workspace.path)
