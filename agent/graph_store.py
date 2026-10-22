"""Validated graph loading and indexes for one repository revision."""
import json
from collections import defaultdict
from pathlib import Path, PurePosixPath

import networkx as nx


NODE_TYPES = {"Repository", "Revision", "File", "Function", "Class", "CallSite"}
RELATIONS = {
    "HAS_REVISION": {("Repository", "Revision")},
    "CONTAINS": {("Revision", "File"), ("File", "CallSite")},
    "DEFINES": {("File", "Function"), ("File", "Class")},
    "HAS_CALL_SITE": {("Function", "CallSite")},
}


def _string(value, label):
    if not isinstance(value, str) or not value:
        raise ValueError(f"Expected nonempty string: {label}")
    return value


class GraphStore:
    def __init__(self, graph_path: Path, revision: str | None = None, source_uri: str | None = None):
        data = json.loads(Path(graph_path).read_text(encoding="utf-8"))
        if not isinstance(data, dict) or type(data.get("schema_version")) is not int or data["schema_version"] != 1:
            raise ValueError("Expected graph schema_version 1")
        if set(data) != {"schema_version", "nodes", "edges"}:
            raise ValueError("Unexpected graph fields")
        if not isinstance(data["nodes"], list) or not isinstance(data["edges"], list):
            raise ValueError("Graph nodes and edges must be arrays")
        nodes = {}
        for node in data["nodes"]:
            if not isinstance(node, dict) or set(node) != {"id", "type", "properties"}:
                raise ValueError("Invalid node record")
            nid = _string(node["id"], "node.id")
            if nid in nodes:
                raise ValueError(f"Duplicate node ID: {nid}")
            if node["type"] not in NODE_TYPES or not isinstance(node["properties"], dict):
                raise ValueError(f"Invalid node type or properties: {nid}")
            nodes[nid] = node
        edges = []
        edge_keys = set()
        for edge in data["edges"]:
            if not isinstance(edge, dict) or set(edge) != {"source", "target", "type", "properties"}:
                raise ValueError("Invalid edge record")
            source = _string(edge["source"], "edge.source")
            target = _string(edge["target"], "edge.target")
            relation = _string(edge["type"], "edge.type")
            if source not in nodes or target not in nodes:
                raise ValueError(f"Dangling edge: {source} -> {target}")
            if relation not in RELATIONS or (nodes[source]["type"], nodes[target]["type"]) not in RELATIONS[relation]:
                raise ValueError(f"Invalid relationship: {relation}")
            if not isinstance(edge["properties"], dict):
                raise ValueError("Edge properties must be an object")
            key = (source, target, relation)
            if key in edge_keys:
                raise ValueError(f"Duplicate relationship: {key}")
            edge_keys.add(key)
            edges.append(edge)
        # Validate everything before NetworkX can implicitly create missing endpoints.
        graph = nx.MultiDiGraph()
        for nid in sorted(nodes):
            graph.add_node(nid, type=nodes[nid]["type"], properties=nodes[nid]["properties"])
        for edge in sorted(edges, key=lambda e: (e["source"], e["target"], e["type"])):
            graph.add_edge(edge["source"], edge["target"], key=edge["type"],
                           type=edge["type"], properties=edge["properties"])
        choices = []
        for nid, attrs in graph.nodes(data=True):
            if attrs["type"] != "Revision":
                continue
            parents = list(graph.predecessors(nid))
            if len(parents) != 1 or graph.nodes[parents[0]]["type"] != "Repository":
                raise ValueError("Each revision needs exactly one repository")
            repo = parents[0]
            uri = _string(graph.nodes[repo]["properties"].get("uri"), "repository.uri")
            commit = _string(attrs["properties"].get("commit"), "revision.commit")
            choices.append((uri, commit, repo, nid))
        matches = [choice for choice in choices if (revision is None or choice[1] == revision)
                   and (source_uri is None or choice[0] == source_uri)]
        if len(matches) != 1:
            available = sorted({(c[0], c[1]) for c in choices})
            raise ValueError(f"Select exactly one repository/revision with --source-uri and --revision; available: {available}")
        self.source_uri, self.revision, repo_id, revision_id = matches[0]
        selected = {repo_id, revision_id}
        file_ids = set(graph.successors(revision_id))
        for fid in file_ids:
            parents = list(graph.predecessors(fid))
            if parents != [revision_id]:
                raise ValueError("File belongs to multiple revisions")
            selected.add(fid)
            selected.update(graph.successors(fid))
        # Calls must stay within their file; containment determines ownership.
        self.node_files = {}
        self.files = defaultdict(list)
        for fid in sorted(file_ids):
            props = graph.nodes[fid]["properties"]
            path = _string(props.get("path"), "file.path")
            pure = PurePosixPath(path)
            if pure.is_absolute() or ".." in pure.parts or ".git" in pure.parts:
                raise ValueError(f"Unsafe repository path: {path}")
            provenance = props.get("provenance", {})
            if provenance.get("source_id") != self.source_uri or provenance.get("snapshot_id") != self.revision:
                raise ValueError(f"File provenance differs from graph revision: {path}")
            self.files[path].append(fid)
            self.node_files[fid] = fid
            for child in graph.successors(fid):
                if child in self.node_files:
                    raise ValueError(f"Symbol belongs to multiple files: {child}")
                self.node_files[child] = fid
        for source, target, relation in graph.edges(keys=True):
            if source in selected and relation == "HAS_CALL_SITE":
                if target not in selected or self.node_files.get(source) != self.node_files.get(target):
                    raise ValueError("Call site and caller must belong to the same file")
        self.graph = graph.subgraph(selected).copy()
        self.names = defaultdict(list)
        self.qualified_names = defaultdict(list)
        for nid, attrs in self.graph.nodes(data=True):
            for field, index in (("name", self.names), ("qualified_name", self.qualified_names)):
                value = attrs["properties"].get(field)
                if isinstance(value, str) and value:
                    index[value].append(nid)

    def describe(self, node_id: str) -> dict:
        if node_id not in self.graph:
            raise ValueError(f"Unknown node ID: {node_id}")
        attrs = self.graph.nodes[node_id]
        fid = self.node_files.get(node_id)
        path = self.graph.nodes[fid]["properties"]["path"] if fid else None
        return {"id": node_id, **attrs, "path": path}

    def status(self) -> dict:
        partial = [p for p, ids in self.files.items() if any(
            self.graph.nodes[fid]["properties"].get("parse_status") == "partial" for fid in ids)]
        return {"source_uri": self.source_uri, "revision": self.revision,
                "nodes": len(self.graph), "edges": self.graph.number_of_edges(),
                "files": len(self.files), "partial_files": partial,
                "supported_relations": sorted(RELATIONS),
                "capabilities": {"resolved_calls": False, "impact_analysis": False},
                "coverage": "Exported files only; includes, lexical scopes and resolved calls are not represented."}
