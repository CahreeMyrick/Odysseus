# Architecture

## Local workflow

1. The C++ GitHub adapter resolves a ref to a commit and fetches supported files.
2. Tree-sitter extracts code models; document processing retains text.
3. Graph export validates models and creates repository, revision, file, symbol,
   and unresolved call-site nodes.
4. Python loads the graph with a local checkout and exposes bounded inspection tools.
5. The model selects tools and returns an answer with evidence references.

Graph relationships are `HAS_REVISION`, `CONTAINS`, `DEFINES`, and `HAS_CALL_SITE`.
There are no resolved `CALLS` edges. Graph schema v1 is defined in
[graph.schema.json](../schemas/graph.schema.json).

## Python boundaries

| Module | Responsibility |
|---|---|
| `graph_store.py` | Validate graph records and select repository/revision |
| `repository.py` | Bound source reads and check checkout identity |
| `retrieval.py` | Lexical ranking and bounded, typed graph traversal |
| `tools.py`, `tool_registry.py` | Explicit read-only model tool interface |
| `runtime.py` | Ollama adapter and bounded conversation loop |
| `evidence.py`, `tracing.py` | Evidence identity, citations, and optional traces |
| `evaluation.py`, `benchmarks/` | Controlled comparisons and review criteria |

Exact symbol reads require matching checkout HEAD and committed file bytes.
Ordinary file reads identify current content by hash. Reads reject `.git` paths
and symlinks escaping the selected root. Search is literal, not embedding-based.
Repository text is untrusted input; the model receives no editing or shell tools.

Citations establish which evidence reached the model. They do not certify semantic
support. Source traces are written only when requested and never overwrite files.

## Optional services

PostgreSQL stores artifacts and indexing tasks. Separate workers write embeddings
to Qdrant or graph projections to Neo4j. These service projections are distinct
from the local graph export; the Python inspector loads JSON, not those services.
See [service configuration](ingestion.md#store-and-index-with-external-services).
