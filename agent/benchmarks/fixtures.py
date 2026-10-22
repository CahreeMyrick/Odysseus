"""Synthetic code graph shared by deterministic tests and example benchmarks."""
from pathlib import Path

FIXTURE = Path(__file__).resolve().parents[1] / "tests" / "fixtures" / "repo"


def make_graph(revision: str) -> dict:
    nodes, edges = [], []
    uri = "https://github.com/example/fixture"
    def node(nid, kind, **props):
        nodes.append({"id": nid, "type": kind, "properties": props})
    def edge(source, kind, target):
        edges.append({"source": source, "target": target, "type": kind, "properties": {}})
    node("repo", "Repository", uri=uri)
    node("revision", "Revision", commit=revision)
    edge("repo", "HAS_REVISION", "revision")
    for path, name, qname in (("main.cpp", "run", "run"), ("helpers.cpp", "helper", "helper"),
                              ("other.cpp", "helper", "other::helper")):
        raw = (FIXTURE / path).read_bytes()
        start = raw.index(b"int ")
        end = raw.index(b"}", start) + 1
        span = {"start_byte": start, "end_byte": end,
                "start_line": raw[:start].count(b"\n") + 1, "end_line": raw[:end].count(b"\n") + 1}
        fid, fnid = f"file:{path}", f"function:{path}"
        node(fid, "File", path=path, name=path, provenance={"source_id": uri, "snapshot_id": revision},
             model_type="code", parse_status="complete")
        node(fnid, "Function", name=name, qualified_name=qname, signature=f"int {name}()", span=span)
        edge("revision", "CONTAINS", fid)
        edge(fid, "DEFINES", fnid)
    raw_main = (FIXTURE / "main.cpp").read_bytes()
    call_start = raw_main.index(b"helper()")
    node("call:helper", "CallSite", expression="helper", callee="helper", resolution_status="unresolved",
         span={"start_byte": call_start, "end_byte": call_start + len(b"helper()"), "start_line": 3, "end_line": 3})
    edge("file:main.cpp", "CONTAINS", "call:helper")
    edge("function:main.cpp", "HAS_CALL_SITE", "call:helper")
    return {"schema_version": 1, "nodes": nodes, "edges": edges}

