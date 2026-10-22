"""Narrow model-callable inspection tools returning structured evidence."""
import inspect

from .config import Config
from .evidence import EvidenceStore
from .graph_store import GraphStore
from .repository import Repository
from . import retrieval
from .schemas import bounded_int, encode, result, text


class InspectionTools:
    def __init__(self, repository: Repository, graph: GraphStore | None,
                 evidence: EvidenceStore, config: Config):
        self.repository, self.graph, self.evidence, self.config = repository, graph, evidence, config

    def available(self) -> dict:
        names = ["repository_status", "read_file", "search_source"]
        if self.graph is not None:
            names += ["find_symbols", "describe_symbol", "get_neighbors", "find_path", "read_symbol_source"]
        return {name: getattr(self, name) for name in names}

    def execute(self, name: str, arguments: dict) -> dict:
        """Validate invocation and bound responses before they reach the model."""
        try:
            if not isinstance(arguments, dict):
                raise ValueError("Tool arguments must be an object")
            tool = self.available().get(name)
            if tool is None:
                raise ValueError(f"Unknown or unavailable tool: {name}")
            inspect.signature(tool).bind(**arguments)
            output = tool(**arguments)
        except NotImplementedError as exc:
            output = result("unsupported", data={"message": str(exc)})
        except (ValueError, TypeError, KeyError, OSError) as exc:
            output = result("error", data={"message": str(exc)})
        if len(encode(output)) > self.config.max_result_chars:
            return result("error", data={"message": "Result exceeds tool response budget; narrow the query or source range."},
                          truncated=True, limitations=["No evidence from this oversized response was delivered."])
        return output

    def _store(self) -> GraphStore:
        if self.graph is None:
            raise NotImplementedError("Graph tools are disabled in source-only mode")
        return self.graph

    def _graph_result(self, data: dict, status: str = "ok", limitations: list[str] | None = None) -> dict:
        store = self._store()
        evidence_ids = []
        evidence_map = {}
        # Evidence is attached to actual returned nodes/edges, not just a successful query.
        for node in data.get("nodes", []) + data.get("matches", []):
            props = node["properties"]
            span = props.get("span", {})
            record = {"kind": "graph_node", "revision": store.revision, "source_uri": store.source_uri,
                      "node_id": node["id"], "path": node.get("path"), "data": node,
                      "start_line": span.get("start_line"), "end_line": span.get("end_line")}
            eid = self.evidence.add(record)
            evidence_ids.append(eid)
            evidence_map[eid] = {"node_id": node["id"], "path": node.get("path")}
        for edge in data.get("edges", []):
            eid = self.evidence.add({"kind": "graph_edge", "revision": store.revision,
                "source_uri": store.source_uri, "data": edge})
            evidence_ids.append(eid)
            evidence_map[eid] = {key: edge[key] for key in ("source", "target", "type")}
        data["evidence_map"] = evidence_map
        notes = list(limitations or [])
        partial_paths = set(store.status()["partial_files"])
        if any(node.get("path") in partial_paths for node in data.get("nodes", []) + data.get("matches", [])):
            notes.append("Some returned files have partial parses; extracted structure may be incomplete.")
        if self.repository.head() != store.revision:
            notes.append("Graph revision differs from current checkout or checkout revision is unknown.")
        return result(status, data=data, evidence_ids=evidence_ids, revision=store.revision,
                      truncated=data.get("truncated", False), limitations=notes)

    def repository_status(self) -> dict:
        """Report current checkout identity and available graph capabilities and coverage."""
        revision = self.graph.revision if self.graph else None
        status = self.repository.status(revision)
        status["mode"] = "graph" if self.graph else "source-only"
        if self.graph:
            status["graph"] = self.graph.status()
        return result(data=status, revision=status["head"], limitations=[
            "A clean checkout and matching revision do not establish complete graph coverage."])

    def find_symbols(self, query: str, path_filter: str = "", limit: int = 10) -> dict:
        """Find candidate functions, classes or files. Names can be ambiguous; use returned IDs.

        Args:
            query: Exact ID, qualified name, short name or lexical search terms.
            path_filter: Optional substring of a repository-relative path.
            limit: Maximum candidates, from 1 to 30.
        """
        text(query, "query"); text(path_filter, "path_filter", allow_empty=True)
        bounded_int(limit, "limit", 1, 30)
        data = retrieval.find_symbols(self._store(), query, path_filter, limit)
        status = "ambiguous" if data["ambiguous"] else "ok" if data["matches"] else "not_found"
        return self._graph_result(data, status)

    def describe_symbol(self, node_id: str) -> dict:
        """Return a graph node's properties and source location by exact node ID."""
        text(node_id, "node_id")
        return self._graph_result({"nodes": [self._store().describe(node_id)]})

    def get_neighbors(self, node_id: str, relations: list[str] | None = None,
                      direction: str = "out", depth: int = 1, limit: int = 30) -> dict:
        """Explore typed graph relationships using bounded BFS; source/target direction is preserved.

        Args:
            node_id: Exact starting node ID.
            relations: Relationship names; omitted means all available structural relationships.
            direction: out, in, or both.
            depth: Number of hops, from 1 to 3.
            limit: Maximum returned edges, from 1 to 100.
        """
        text(node_id, "node_id")
        if direction not in ("out", "in", "both"):
            raise ValueError("direction must be out, in or both")
        bounded_int(depth, "depth", 1, 3); bounded_int(limit, "limit", 1, 100)
        allowed = retrieval.relations([] if relations is None else relations)
        data = retrieval.neighborhood(self._store(), node_id, allowed, direction, depth, limit)
        return self._graph_result(data, limitations=["Results are limited to the requested hop depth and relationship types."])

    def find_path(self, source_id: str, target_id: str, relations: list[str], max_hops: int = 6) -> dict:
        """Find a directed shortest path through explicitly selected relationship types.

        Args:
            source_id: Exact starting node ID.
            target_id: Exact destination node ID.
            relations: Nonempty list of allowed relationship names. CALLS is not currently supported.
            max_hops: Maximum path length, from 1 to 12.
        """
        text(source_id, "source_id"); text(target_id, "target_id")
        bounded_int(max_hops, "max_hops", 1, 12)
        allowed = retrieval.relations(relations, required=True)
        data = retrieval.shortest_path(self._store(), source_id, target_id, allowed, max_hops)
        return self._graph_result(data, "ok" if data["found"] else "not_found", limitations=[
            "A structural path is not a runtime execution trace.",
            "No path means none found within these relationships and bounds, not proof of no dependency."])

    def _source_result(self, path: str, raw: bytes, content: str, start: int, end: int,
                       truncated: bool = False, extra: dict | None = None) -> dict:
        record = self.repository.source_record(path, raw, start, end, content)
        eid = self.evidence.add(record)
        return result(data=record | (extra or {}), evidence_ids=[eid], revision=record["revision"],
                      truncated=truncated, limitations=["Source is the current working-tree file, identified by its content hash."])

    def read_file(self, path: str, start_line: int = 1, end_line: int = 120) -> dict:
        """Read a bounded UTF-8 source range from the current checkout, not from the graph snapshot.

        Args:
            path: Repository-relative file path.
            start_line: First one-based line.
            end_line: Last inclusive line; at most 200 lines per call.
        """
        text(path, "path")
        bounded_int(start_line, "start_line", 1, 10000000)
        bounded_int(end_line, "end_line", start_line, start_line + self.config.max_source_lines - 1)
        raw, content = self.repository.read(path)
        lines = content.splitlines(keepends=True)
        if start_line > len(lines) and not (not lines and start_line == 1):
            raise ValueError("start_line is past end of file")
        actual_end = min(end_line, len(lines))
        return self._source_result(path, raw, "".join(lines[start_line - 1:actual_end]), start_line, actual_end,
                                   extra={"total_lines": len(lines)})

    def read_symbol_source(self, node_id: str) -> dict:
        """Read the exact graph source span only if the checkout and file match the graph revision.

        Args:
            node_id: Exact graph symbol or call-site ID with a byte span.
        """
        text(node_id, "node_id")
        store = self._store()
        node = store.describe(node_id)
        path = node["path"]
        span = node["properties"].get("span")
        if not path or not isinstance(span, dict):
            raise ValueError("Node has no source span; use read_file for files")
        raw, _ = self.repository.read(path)
        if not self.repository.symbol_source_matches(path, raw, store.revision):
            return result("stale", data={"path": path, "node_id": node_id,
                "message": "Cannot verify graph offsets against this checkout. Use read_file or search_source for current code."},
                revision=store.revision)
        start, end = span.get("start_byte"), span.get("end_byte")
        if type(start) is not int or type(end) is not int or not 0 <= start <= end <= len(raw):
            raise ValueError("Invalid source byte span")
        content = raw[start:end].decode("utf-8")
        start_line = raw[:start].count(b"\n") + 1
        lines = content.splitlines(keepends=True)
        truncated = len(lines) > self.config.max_source_lines
        if truncated:
            content = "".join(lines[:self.config.max_source_lines])
        end_line = start_line + max(0, len(content.splitlines()) - 1)
        return self._source_result(path, raw, content, start_line, end_line, truncated,
                                   {"node_id": node_id, "graph_revision": store.revision})

    def search_source(self, query: str, path_filter: str = "", limit: int = 20) -> dict:
        """Search literal text in current UTF-8 files, excluding build/cache directories.

        Args:
            query: Nonempty literal text, matched case-insensitively (not a regex).
            path_filter: Optional repository-relative path substring.
            limit: Maximum matching lines, from 1 to 50.
        """
        text(query, "query"); text(path_filter, "path_filter", allow_empty=True)
        bounded_int(limit, "limit", 1, 50)
        data = self.repository.search(query, path_filter, limit)
        ids = [self.evidence.add(match) for match in data["matches"]]
        return result("ok" if ids else "not_found", data=data, evidence_ids=ids,
                      revision=self.repository.head(), truncated=data["truncated"], limitations=[
                          "Literal search of current working tree; ignored directories, binary, oversized and unreadable files are excluded."])
