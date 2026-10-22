"""Deterministic lexical ranking and bounded, typed graph traversal."""
from collections import deque
import re

from .graph_store import GraphStore, RELATIONS


def find_symbols(store: GraphStore, query: str, path_filter: str, limit: int) -> dict:
    terms = re.findall(r"[a-z0-9_]+", query.casefold())
    exact = set(store.names.get(query, [])) | set(store.qualified_names.get(query, []))
    if query in store.graph:
        exact.add(query)
    ranked = []
    for nid, attrs in store.graph.nodes(data=True):
        if attrs["type"] not in {"Function", "Class", "File"}:
            continue
        node = store.describe(nid)
        if path_filter and path_filter not in (node["path"] or ""):
            continue
        props = node["properties"]
        haystack = " ".join(str(props.get(k, "")) for k in ("name", "qualified_name", "signature", "path")).casefold()
        score = 1000 if nid in exact else sum(term in haystack for term in terms)
        if score:
            ranked.append((score, nid))
    ranked.sort(key=lambda item: (-item[0], item[1]))
    filtered_exact = [nid for _, nid in ranked if nid in exact]
    return {"matches": [store.describe(nid) | {"score": score} for score, nid in ranked[:limit]],
            "ambiguous": len(filtered_exact) > 1, "truncated": len(ranked) > limit}


def relations(requested: list[str], *, required: bool = False) -> set[str]:
    if not isinstance(requested, list) or not all(isinstance(r, str) for r in requested):
        raise ValueError("relations must be a list of relationship names")
    if required and not requested:
        raise ValueError("Choose explicit relationship types for path finding")
    unknown = set(requested) - RELATIONS.keys()
    if unknown:
        raise NotImplementedError(f"Unsupported relationships: {sorted(unknown)}; resolved CALLS is unavailable")
    return set(requested) if requested else set(RELATIONS)


def steps(store: GraphStore, node: str, allowed: set[str], direction: str):
    edges = []
    if direction in ("out", "both"):
        edges.extend(store.graph.out_edges(node, keys=True, data=True))
    if direction in ("in", "both"):
        edges.extend(store.graph.in_edges(node, keys=True, data=True))
    for source, target, key, attrs in sorted(edges, key=lambda e: (e[0], e[1], e[2])):
        if attrs["type"] in allowed:
            neighbor = target if source == node else source
            yield neighbor, {"source": source, "target": target, "type": attrs["type"],
                             "properties": attrs["properties"]}


def neighborhood(store: GraphStore, node_id: str, allowed: set[str], direction: str,
                 depth: int, limit: int) -> dict:
    store.describe(node_id)
    queue = deque([(node_id, 0)])
    visited, edges, edge_ids = {node_id}, [], set()
    truncated = False
    while queue:
        current, distance = queue.popleft()
        if distance == depth:
            continue
        for neighbor, edge in steps(store, current, allowed, direction):
            key = (edge["source"], edge["target"], edge["type"])
            if key in edge_ids:
                continue
            if len(edges) >= limit:
                truncated = True
                break
            edge_ids.add(key)
            edges.append(edge)
            if neighbor not in visited:
                visited.add(neighbor)
                queue.append((neighbor, distance + 1))
        if truncated:
            break
    return {"nodes": [store.describe(nid) for nid in sorted(visited)], "edges": edges,
            "depth": depth, "truncated": truncated}


def shortest_path(store: GraphStore, source: str, target: str, allowed: set[str], max_hops: int,
                  max_visited: int = 5000) -> dict:
    store.describe(source)
    store.describe(target)
    queue = deque([(source, 0)])
    parents = {source: None}
    truncated = False
    while queue:
        current, depth = queue.popleft()
        if current == target:
            path_nodes, path_edges = [target], []
            while parents[path_nodes[-1]] is not None:
                previous, edge = parents[path_nodes[-1]]
                path_edges.append(edge)
                path_nodes.append(previous)
            return {"nodes": [store.describe(nid) for nid in reversed(path_nodes)],
                    "edges": list(reversed(path_edges)), "found": True, "truncated": False}
        if depth == max_hops:
            continue
        for neighbor, edge in steps(store, current, allowed, "out"):
            if neighbor in parents:
                continue
            if len(parents) >= max_visited:
                truncated = True
                break
            parents[neighbor] = (current, edge)
            queue.append((neighbor, depth + 1))
        if truncated:
            break
    return {"nodes": [], "edges": [], "found": False, "truncated": truncated,
            "max_hops": max_hops, "visited": len(parents)}
