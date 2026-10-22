"""Read-only, Graphify-style CLI for the airline policy graph."""

import argparse
import json

import networkx as nx
from policy_graph import build_policy_graph


def describe(graph: nx.MultiDiGraph, node: str) -> dict:
    if node not in graph:
        return {"error": f"Unknown node: {node}"}
    return {
        "node": node,
        "properties": dict(graph.nodes[node]),
        "outgoing": [
            {"relation": data["relation"], "target": target,
             "properties": {key: value for key, value in data.items() if key != "relation"},
             "target_properties": dict(graph.nodes[target])}
            for _, target, data in graph.out_edges(node, data=True)
        ],
    }


def query(graph: nx.MultiDiGraph, text: str) -> dict:
    """Simple lexical matching; not semantic retrieval."""
    words = set(text.lower().replace("_", " ").split())
    scores = []
    for node, data in graph.nodes(data=True):
        haystack = f"{node} {data.get('description', '')}".lower()
        score = sum(word in haystack for word in words if len(word) > 2)
        if score:
            scores.append((score, node))
    ranked = [node for _, node in sorted(scores, key=lambda pair: (-pair[0], pair[1]))[:5]]
    return {"matches": [describe(graph, node) for node in ranked]}


def main() -> None:
    parser = argparse.ArgumentParser(description="Query airline policy graph")
    sub = parser.add_subparsers(dest="command", required=True)
    for command in ("query", "explain", "path"):
        p = sub.add_parser(command)
        p.add_argument("argument")
        if command == "path":
            p.add_argument("target")
    args = parser.parse_args()
    graph = build_policy_graph()
    if args.command == "query":
        result = query(graph, args.argument)
    elif args.command == "explain":
        result = describe(graph, args.argument)
    else:
        try:
            nodes = nx.shortest_path(graph, args.argument, args.target)
            result = {"nodes": nodes, "edges": [
                {"source": a, "target": b, "relations": [d["relation"] for d in graph[a][b].values()]}
                for a, b in zip(nodes, nodes[1:])
            ]}
        except (nx.NodeNotFound, nx.NetworkXNoPath) as exc:
            result = {"error": str(exc)}
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()

