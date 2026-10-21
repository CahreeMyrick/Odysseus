import json
import unittest

from policy_validation import validate_policy_response, validate_tool_call


class PolicyValidationTests(unittest.TestCase):
    def test_command_specific_evidence(self):
        node = {"node": "cancellation", "properties": {}, "outgoing": []}
        cases = [
            ({"command": "query", "argument": "cancel"}, {"matches": [node]}),
            ({"command": "explain", "argument": "cancellation"}, node),
            ({"command": "path", "argument": "a", "target": "b"},
             {"nodes": ["a", "b"], "edges": [{"source": "a", "target": "b", "relations": ["requires"]}]}),
            ({"command": "path", "argument": "a", "target": "a"},
             {"nodes": ["a"], "edges": []}),
        ]
        for arguments, response in cases:
            with self.subTest(arguments=arguments):
                self.assertTrue(validate_policy_response(arguments, json.dumps(response)))

    def test_failed_or_unstructured_responses_do_not_count(self):
        arguments = {"command": "query", "argument": "cancel"}
        for output in ["Error: invalid arguments", "Policy CLI error: failed", "invalid JSON",
                       "null", "[]", "{}", '{"unrelated": true}', '{"matches": []}',
                       '{"matches": ["not a node"]}', '{"error": "missing"}']:
            with self.subTest(output=output):
                self.assertFalse(validate_policy_response(arguments, output))

    def test_mismatched_nodes_and_broken_paths_are_rejected(self):
        self.assertFalse(validate_policy_response(
            {"command": "explain", "argument": "a"},
            json.dumps({"node": "b", "properties": {}, "outgoing": []})))
        arguments = {"command": "path", "argument": "a", "target": "b"}
        for response in [
            {"nodes": [], "edges": []},
            {"nodes": ["a", "b"], "edges": []},
            {"nodes": ["a", "b"], "edges": [{"source": "b", "target": "a", "relations": ["requires"]}]},
        ]:
            self.assertFalse(validate_policy_response(arguments, json.dumps(response)))

    def test_tool_prerequisites_use_episode_log(self):
        tools = {"run_policy", "get_reservation", "cancel_reservation"}
        queries = []
        self.assertIsNone(validate_tool_call("run_policy", tools, queries))
        for name in ("get_reservation", "cancel_reservation"):
            self.assertIsNotNone(validate_tool_call(name, tools, queries))
        queries.append({"succeeded": False})
        self.assertIsNotNone(validate_tool_call("get_reservation", tools, queries))
        queries.append({"succeeded": True})
        queries.append({"succeeded": False})
        for name in ("get_reservation", "cancel_reservation"):
            self.assertIsNone(validate_tool_call(name, tools, queries))
        self.assertEqual(validate_tool_call("unknown", tools, queries), "Error: unknown tool")


if __name__ == "__main__":
    unittest.main()
