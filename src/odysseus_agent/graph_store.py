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
    "INFERRED_TARGET": {("CallSite", "Function")},
    "EXTRACTED_TARGET": {("CallSite", "Function")},
    "DECLARES": {("File", "Function")},
    "DECLARATION_OF": {("Function", "Function")},
    "INCLUDES": {("File", "File")},
}


def _string(value, label):
    if not isinstance(value, str) or not value:
        raise ValueError(f"Expected nonempty string: {label}")
    return value


class GraphStore:
    def __init__(
        self, graph_path: Path, revision: str | None = None, source_uri: str | None = None
    ):
        data = json.loads(Path(graph_path).read_text(encoding="utf-8"))
        if (
            not isinstance(data, dict)
            or type(data.get("schema_version")) is not int
            or data["schema_version"] not in {1, 2, 3, 4}
        ):
            raise ValueError("Expected graph schema_version 1, 2, 3 or 4")
        self.schema_version = data["schema_version"]
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
            if not isinstance(edge, dict) or set(edge) != {
                "source",
                "target",
                "type",
                "properties",
            }:
                raise ValueError("Invalid edge record")
            source = _string(edge["source"], "edge.source")
            target = _string(edge["target"], "edge.target")
            relation = _string(edge["type"], "edge.type")
            if source not in nodes or target not in nodes:
                raise ValueError(f"Dangling edge: {source} -> {target}")
            if (
                relation not in RELATIONS
                or (nodes[source]["type"], nodes[target]["type"]) not in RELATIONS[relation]
            ):
                raise ValueError(f"Invalid relationship: {relation}")
            if not isinstance(edge["properties"], dict):
                raise ValueError("Edge properties must be an object")
            props = edge["properties"]
            if relation in {"DECLARES", "DECLARATION_OF", "INCLUDES"} and self.schema_version < 4:
                raise ValueError("Declaration relationships require graph v4")
            if relation == "DECLARES" and nodes[target]["properties"].get("kind") != "declaration":
                raise ValueError("DECLARES requires a declaration target")
            if relation == "INCLUDES":
                if (
                    props.get("evidence_kind") not in {"EXTRACTED", "INFERRED"}
                    or props.get("method") not in {"relative_include", "unique_include_suffix"}
                    or props.get("include_kind") not in {"quoted", "unknown"}
                    or (
                        props.get("evidence_kind") == "EXTRACTED"
                        and (
                            props.get("method") != "relative_include"
                            or props.get("include_kind") != "quoted"
                        )
                    )
                ):
                    raise ValueError("Invalid include association")
                evidence = props.get("evidence")
                if (
                    not isinstance(evidence, list)
                    or not all(isinstance(nid, str) for nid in evidence)
                    or set(evidence) != {source, target}
                ):
                    raise ValueError("Invalid include evidence")
                for field in ("raw_include", "resolver_version", "limitations"):
                    _string(props.get(field), field)
            if relation == "DECLARATION_OF":
                if (
                    nodes[source]["properties"].get("kind") != "declaration"
                    or nodes[target]["properties"].get("kind") != "function"
                    or props.get("method") != "exact_sibling_signature"
                    or not nodes[source]["properties"].get("signature_key")
                    or any(
                        nodes[source]["properties"].get(key) != nodes[target]["properties"].get(key)
                        for key in ("qualified_name", "signature_key")
                    )
                ):
                    raise ValueError("Invalid declaration/definition association")
                evidence = props.get("evidence")
                if (
                    not isinstance(evidence, list)
                    or not all(isinstance(nid, str) and nid in nodes for nid in evidence)
                    or source not in evidence
                    or target not in evidence
                ):
                    raise ValueError("Invalid declaration evidence")
                _string(props.get("resolver_version"), "resolver_version")
                _string(props.get("limitations"), "limitations")
            if relation in {"INFERRED_TARGET", "EXTRACTED_TARGET"}:
                extracted = relation == "EXTRACTED_TARGET"
                if self.schema_version < (3 if extracted else 2) or props.get("evidence_kind") != (
                    "EXTRACTED" if extracted else "INFERRED"
                ):
                    raise ValueError("Target evidence kind or graph version is incompatible")
                if props.get("resolution_status") not in (
                    {"extracted"} if extracted else {"inferred", "ambiguous"}
                ):
                    raise ValueError("Invalid target status")
                if extracted and props.get("method") not in {
                    "explicit_qualified_name",
                    "explicit_this_receiver",
                }:
                    raise ValueError("Extracted targets require an explicit syntax method")
                for field in ("method", "resolver_version", "matched_name", "limitations"):
                    _string(props.get(field), field)
                evidence = props.get("evidence")
                if (
                    not isinstance(evidence, list)
                    or not all(isinstance(nid, str) and nid in nodes for nid in evidence)
                    or source not in evidence
                    or target not in evidence
                ):
                    raise ValueError("Invalid target evidence")
                if (
                    extracted
                    and props["method"] == "explicit_this_receiver"
                    and not any(nodes[nid]["type"] == "Class" for nid in evidence)
                ):
                    raise ValueError("Explicit this targets require class evidence")
            elif (
                relation != "INCLUDES"
                and self.schema_version >= 2
                and props.get("evidence_kind") != "EXTRACTED"
            ):
                raise ValueError("Structural edges require EXTRACTED evidence")
            key = (source, target, relation)
            if key in edge_keys:
                raise ValueError(f"Duplicate relationship: {key}")
            edge_keys.add(key)
            edges.append(edge)
        # Validate semantic edge ownership across the entire input, before selection
        # can hide a cross-revision edge or its evidence.
        node_files = {}
        file_revisions = {}
        for edge in edges:
            if nodes[edge["source"]]["type"] == "File" and edge["type"] != "INCLUDES":
                if edge["target"] in node_files:
                    raise ValueError("Symbol belongs to multiple files")
                node_files[edge["target"]] = edge["source"]
            if nodes[edge["source"]]["type"] == "Revision":
                if edge["target"] in file_revisions:
                    raise ValueError("File belongs to multiple revisions")
                file_revisions[edge["target"]] = edge["source"]
        targets = defaultdict(list)
        for edge in edges:
            if edge["type"] not in {
                "INFERRED_TARGET",
                "EXTRACTED_TARGET",
                "DECLARATION_OF",
                "INCLUDES",
            }:
                continue
            if edge["type"] != "INCLUDES":
                targets[edge["source"]].append(edge)
            evidence_files = {
                nid if nodes[nid]["type"] == "File" else node_files.get(nid)
                for nid in edge["properties"]["evidence"]
            }
            revisions = {file_revisions.get(fid) for fid in evidence_files}
            if None in revisions or len(revisions) != 1:
                raise ValueError("Targets and evidence must belong to one revision")
        for matches in targets.values():
            if any(edge["type"] == "DECLARATION_OF" for edge in matches) and len(matches) != 1:
                raise ValueError("A declaration must have a unique associated definition")
            if any(edge["type"] == "EXTRACTED_TARGET" for edge in matches) and len(matches) != 1:
                raise ValueError(
                    "An extracted target must be unique, without inferred alternatives"
                )
        # Validate everything before NetworkX can implicitly create missing endpoints.
        graph = nx.MultiDiGraph()
        for nid in sorted(nodes):
            graph.add_node(nid, type=nodes[nid]["type"], properties=nodes[nid]["properties"])
        for edge in sorted(edges, key=lambda e: (e["source"], e["target"], e["type"])):
            graph.add_edge(
                edge["source"],
                edge["target"],
                key=edge["type"],
                type=edge["type"],
                properties=edge["properties"],
            )
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
        matches = [
            choice
            for choice in choices
            if (revision is None or choice[1] == revision)
            and (source_uri is None or choice[0] == source_uri)
        ]
        if len(matches) != 1:
            available = sorted({(c[0], c[1]) for c in choices})
            raise ValueError(
                f"Select exactly one repository/revision with --source-uri and --revision; available: {available}"
            )
        self.source_uri, self.revision, repo_id, revision_id = matches[0]
        selected = {repo_id, revision_id}
        file_ids = set(graph.successors(revision_id))
        for fid in file_ids:
            parents = [
                source for source, _, key in graph.in_edges(fid, keys=True) if key == "CONTAINS"
            ]
            if parents != [revision_id]:
                raise ValueError("File belongs to multiple revisions")
            selected.add(fid)
            selected.update(
                target for _, target, key in graph.out_edges(fid, keys=True) if key != "INCLUDES"
            )
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
            if (
                provenance.get("source_id") != self.source_uri
                or provenance.get("snapshot_id") != self.revision
            ):
                raise ValueError(f"File provenance differs from graph revision: {path}")
            self.files[path].append(fid)
            self.node_files[fid] = fid
            for _, child, relation in graph.out_edges(fid, keys=True):
                if relation == "INCLUDES":
                    continue
                if child in self.node_files:
                    raise ValueError(f"Symbol belongs to multiple files: {child}")
                self.node_files[child] = fid
        for source, target, relation in graph.edges(keys=True):
            if source in selected and relation == "HAS_CALL_SITE":
                if target not in selected or self.node_files.get(source) != self.node_files.get(
                    target
                ):
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
        partial = [
            p
            for p, ids in self.files.items()
            if any(
                self.graph.nodes[fid]["properties"].get("parse_status") == "partial" for fid in ids
            )
        ]
        return {
            "source_uri": self.source_uri,
            "revision": self.revision,
            "nodes": len(self.graph),
            "edges": self.graph.number_of_edges(),
            "files": len(self.files),
            "partial_files": partial,
            "supported_relations": sorted(
                r
                for r in RELATIONS
                if (r != "INFERRED_TARGET" or self.schema_version >= 2)
                and (r != "EXTRACTED_TARGET" or self.schema_version >= 3)
                and (
                    r not in {"DECLARES", "DECLARATION_OF", "INCLUDES"} or self.schema_version >= 4
                )
            ),
            "capabilities": {
                "resolved_calls": False,
                "inferred_call_targets": self.schema_version >= 2,
                "extracted_call_targets": self.schema_version >= 3,
                "declaration_linking": self.schema_version >= 4,
                "include_associations": self.schema_version >= 4,
                "receiver_type_inference": self.schema_version >= 4
                and any(
                    self.graph.nodes[fid]["properties"].get("parser", {}).get("extractor_version")
                    == "odysseus-code-v3"
                    for ids in self.files.values()
                    for fid in ids
                ),
                "impact_analysis": False,
            },
            "coverage": "Exported files only. Extracted targets are explicit syntax associations, "
            "not compiler-verified calls. Inferred targets are candidates; "
            "scoped receiver bindings and syntactic declaration links are supported in v4; "
            "overload selection, preprocessing, aliases and runtime dispatch are not checked.",
        }
