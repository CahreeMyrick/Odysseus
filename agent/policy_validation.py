"""Validate policy CLI evidence and tool prerequisites without executing tools."""

import json
from collections.abc import Iterable, Mapping


POLICY_REQUIRED_TOOLS = frozenset({"get_reservation", "cancel_reservation"})


def _is_node_description(value: object) -> bool:
    return (
        isinstance(value, dict)
        and "error" not in value
        and isinstance(value.get("node"), str)
        and bool(value["node"].strip())
        and isinstance(value.get("properties"), dict)
        and isinstance(value.get("outgoing"), list)
    )


def validate_policy_response(arguments: Mapping, output: str) -> bool:
    """Accept nonempty evidence matching the requested policy CLI command.

    This validates retrieval, not whether the agent follows the retrieved policy.
    """
    command = arguments.get("command")
    argument = arguments.get("argument")
    target = arguments.get("target", "")
    if not isinstance(argument, str) or not argument.strip():
        return False
    if not isinstance(target, str) or ((command == "path") != bool(target.strip())):
        return False
    try:
        data = json.loads(output)
    except (ValueError, TypeError):
        return False
    if not isinstance(data, dict) or "error" in data:
        return False
    if command == "query":
        matches = data.get("matches")
        return isinstance(matches, list) and bool(matches) and all(
            _is_node_description(match) for match in matches
        )
    if command == "explain":
        return _is_node_description(data) and data["node"] == argument
    if command == "path":
        nodes, edges = data.get("nodes"), data.get("edges")
        if not isinstance(nodes, list) or not nodes or not all(
            isinstance(node, str) and bool(node.strip()) for node in nodes
        ):
            return False
        if nodes[0] != argument or nodes[-1] != target:
            return False
        if not isinstance(edges, list) or len(edges) != len(nodes) - 1:
            return False
        return all(
            isinstance(edge, dict)
            and edge.get("source") == source
            and edge.get("target") == destination
            and isinstance(edge.get("relations"), list)
            and bool(edge["relations"])
            and all(isinstance(relation, str) and bool(relation.strip()) for relation in edge["relations"])
            for edge, source, destination in zip(edges, nodes, nodes[1:])
        )
    return False


def validate_tool_call(
    name: str, available_tools: Iterable[str], policy_queries: Iterable[Mapping]
) -> str | None:
    """Return a rejection reason, or None when the tool's prerequisites hold.

    Query records must have been produced using validate_policy_response.
    Successful consultation persists for the episode, using the log as its
    single source of truth.
    """
    if name not in available_tools:
        return "Error: unknown tool"
    if name in POLICY_REQUIRED_TOOLS and not any(
        query.get("succeeded") is True for query in policy_queries
    ):
        return (
            "Error: Policy consultation required before reservation operations. "
            "Call run_policy with a relevant query or node first."
        )
    return None
