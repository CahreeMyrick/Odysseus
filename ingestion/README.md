# Ingestion service

The ingestion service fetches a GitHub repository at a specific commit, extracts
structured models from supported files, and prints, exports, or stores the results.
It provides two executables:

- **`odysseus`** — ingest repositories and export local JSON knowledge graphs.
- **`odysseus-index`** — process PostgreSQL indexing tasks into Qdrant or Neo4j.

All commands below run from the **Odysseus repository root**.

## Build

Requires CMake 3.18+, a C/C++ compiler with C++17 support, nlohmann-json 3.11+,
PostgreSQL client libraries/headers, and libcurl development libraries/headers.
The PostgreSQL client dependency is required even for local output; a database
server is only needed for PostgreSQL storage or indexing.

On Ubuntu, install the build dependencies with:

```bash
sudo apt-get install build-essential cmake nlohmann-json3-dev libpq-dev libcurl4-openssl-dev
```

Configure and compile both executables:

```bash
cmake -S ingestion -B build/ingestion-debug \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build/ingestion-debug -j 4
```

The first configuration downloads pinned, SHA-256-verified Tree-sitter runtime
and C/C++ grammar sources, so it needs network access. Subsequent source changes
usually require only the build command.

If CMake cannot find a Homebrew installation of `libpq`, repeat configuration
with its location:

```bash
cmake -S ingestion -B build/ingestion-debug \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DPostgreSQL_ROOT="$(brew --prefix libpq)"
```

## Quick start: export models and a graph locally

```bash
./build/ingestion-debug/odysseus ingest github \
  https://github.com/OWNER/REPO --local ./exports/run-1

./build/ingestion-debug/odysseus graph-export ./exports/run-1 \
  --output ./exports/graph.json
```

This produces numbered model files in `exports/run-1/` and a combined graph at
`exports/graph.json`. No database or model server is needed for either command.
Ingestion fetches files through GitHub's API; it does not require a local clone.
Set `GITHUB_TOKEN` in your environment for authenticated requests, including
access to private repositories permitted by that token.

The model directory **must not already exist**; its parents are created as needed.
The graph output **must not already exist**, and its parent directory must exist.
Keep the graph file **outside the model directory**: graph export reads every
regular `.json` file directly in that directory as a model input.

## Ingest a repository

```text
odysseus ingest github <repository-url> [--ref REVISION] [--local DIRECTORY | --postgres]
```

Use a repository URL such as `https://github.com/OWNER/REPO`. `--ref` accepts a
branch, tag, or commit; otherwise the default branch is used. The client resolves
that ref to a commit SHA and records it in each artifact's provenance.

| Output mode | Result |
|---|---|
| No output flag | Extracts models, but prints the **original file contents** and a summary |
| `--local DIRECTORY` | Writes one pretty-printed model JSON file per supported artifact |
| `--postgres` | Stores source text, metadata, models, and indexing tasks in PostgreSQL |

`--local` and `--postgres` are mutually exclusive. Add `--ref COMMIT_SHA` for a
repeatable snapshot. Unsupported files are skipped and counted. Processing stops
on errors; successfully written artifacts remain, so ingestion is not an
all-or-nothing repository transaction.

### Model contents and current limits

Each local file (`1.json`, `2.json`, etc.) contains `artifact_id`, `model_type`,
and `model`. The original path is in `model.file.path`, and repository/commit
information is in `model.file.provenance`.

| Extensions | Processing |
|---|---|
| `.c` | C Tree-sitter grammar |
| `.cpp`, `.cc`, `.cxx`, `.hpp`, `.h` | C++ Tree-sitter grammar; `.h` is treated as C++ |
| `.md`, `.txt` | Document model containing the text |

Code models contain includes, class/struct/union definitions, functions and
lambdas, call sites, lexical scopes, source spans, parser versions, and diagnostics.
They contain extracted structure rather than a complete copy of the source text.
Function declarations without bodies are not collected as function definitions.
Malformed syntax can yield a `partial` model with diagnostics.

Extraction is syntactic: call targets and includes remain unresolved. It does
not perform type checking, cross-file call resolution, or impact analysis.
Occurrence IDs are local to a file and can change when source offsets change.

Only GitHub ingestion is implemented; `local-file` and `database` are placeholders.
There is no single-file filter. Supported input must be UTF-8 text, with limits
of 8 MiB per file and 64 MiB of downloaded text per snapshot. Symlinks and
submodules are not followed; truncated GitHub tree listings are rejected.

## Export a local knowledge graph

```text
odysseus graph-export <model-directory> --output <graph.json>
```

The converter reads code schema v2 and unversioned document models. It writes
`schema_version: 1`, a `nodes` array, and an `edges` array, as defined by
[graph.schema.json](../schemas/graph.schema.json).

| Source | Relationship | Target |
|---|---|---|
| Repository | `HAS_REVISION` | Revision |
| Revision | `CONTAINS` | File |
| File | `DEFINES` | Function or Class |
| File | `CONTAINS` | CallSite |
| Function | `HAS_CALL_SITE` | CallSite |

Node properties preserve file provenance, symbol details, and source locations.
Code file nodes also retain parser metadata, parse status, and diagnostics.
Documents become file nodes; includes and lexical scopes are not projected.
Call sites remain unresolved, so no target-function `CALLS` edges are created.

IDs are opaque strings scoped by repository, revision, file, and extraction
identity. Identical records and shared repository/revision nodes are deduplicated;
conflicting records and unknown caller IDs are rejected. Output is deterministic
for the same records, regardless of input order. All inputs are validated before
creating the output file. Loading is nonrecursive and holds the graph in memory;
an empty model directory produces an empty graph.

The [Python inspection agent](../agent/README.md) can load this graph alongside a
local checkout of the exported repository. Use the matching commit for exact
symbol-source inspection.

## Store and index with external services

For PostgreSQL storage, create a database and configure a connection whose user
can create and write the service's schema:

```bash
export ODYSSEUS_DATABASE_URL='postgresql://USER:PASSWORD@localhost:5432/DATABASE'
./build/ingestion-debug/odysseus ingest github \
  https://github.com/OWNER/REPO --postgres
```

The service initializes its tables under `odysseus_ingestion`. Each artifact,
model, and its vector/graph indexing tasks are saved together in a transaction.
Indexing is a separate step; it consumes stored tasks, not local JSON exports:

```bash
./build/ingestion-debug/odysseus-index vector  # Ollama embeddings → Qdrant
./build/ingestion-debug/odysseus-index graph   # Structured models → Neo4j
./build/ingestion-debug/odysseus-index all     # Both targets
```

Start the required services first. Vector indexing needs the configured embedding
model available in Ollama. It embeds source-text chunks, not graph nodes. Graph
indexing writes nodes and relationships using Neo4j's HTTP query API.

| Environment variable | Default / purpose |
|---|---|
| `ODYSSEUS_DATABASE_URL` | Required for PostgreSQL storage and indexing |
| `QDRANT_URL` | `http://localhost:16333` |
| `QDRANT_COLLECTION` | `odysseus_nomic_v1` |
| `QDRANT_API_KEY` | Optional |
| `OLLAMA_URL` | `http://localhost:11435` |
| `ODYSSEUS_EMBEDDING_MODEL` | `nomic-embed-text` |
| `NEO4J_URL` | `http://localhost:17474` |
| `NEO4J_DATABASE` | `neo4j` |
| `NEO4J_USER` | `neo4j` |
| `NEO4J_PASSWORD` | Set when authentication is required |

These default service ports may differ from your installation. For example, use
`export OLLAMA_URL=http://localhost:11434` for a local Ollama server on port 11434.

Each worker invocation processes up to 100 ready tasks per selected target, then
exits. Rerun it to drain more work. A backend failure stops that target's batch;
recorded failed tasks become retryable after 60 seconds. With `all`, the other
target is still attempted. Neo4j indexing retains its existing task/generation
IDs; these differ from the local graph export's IDs.

## Tests and implementation map

```bash
ctest --test-dir build/ingestion-debug --output-on-failure
```

Database integration tests skip unless `ODYSSEUS_TEST_DATABASE_URL` or
`ODYSSEUS_GITHUB_TEST_DATABASE_URL`, respectively, point to isolated empty test
databases. The GitHub integration test also needs network access.

The processing flow is **fetch snapshot → emit artifacts → classify → extract
models → write storage**. Useful entry points:

| Code | Responsibility |
|---|---|
| [main.cpp](src/main.cpp), [cli.cpp](src/cli.cpp) | CLI dispatch and configuration |
| [rest_github_client.cpp](src/rest_github_client.cpp), [github_adapter.cpp](src/github_adapter.cpp) | Snapshot fetching and artifact creation |
| [pipeline.cpp](src/pipeline.cpp), [processors.cpp](src/processors.cpp) | Classification, processing, and storage orchestration |
| [code_extractor.cpp](src/code_extractor.cpp) | Tree-sitter traversal and syntax extraction |
| [model_json.hpp](src/model_json.hpp), [schemas](../schemas/) | Model types and serialization |
| [storage.cpp](src/storage.cpp), [postgres_storage.cpp](src/postgres_storage.cpp) | Print, local JSON, and PostgreSQL storage |
| [graph_export.cpp](src/graph_export.cpp) | Reusable local graph conversion |
| [index_main.cpp](src/index_main.cpp), [indexing.cpp](src/indexing.cpp) | Indexing worker and backend projections |
