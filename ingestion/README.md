# Ingestion

C++17 service for fetching GitHub snapshots, extracting C/C++ syntax and document
models, and exporting local knowledge graphs. PostgreSQL storage and Qdrant/Neo4j
indexing are optional workflows.

## Build

Requires CMake 3.18+, a C/C++ compiler, nlohmann-json 3.11+, libpq, and libcurl.
On Ubuntu:

```sh
sudo apt-get install build-essential cmake nlohmann-json3-dev libpq-dev libcurl4-openssl-dev
cmake -S ingestion -B build/ingestion-debug -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build/ingestion-debug -j 4
ctest --test-dir build/ingestion-debug --output-on-failure
```

Run from the repository root. Initial configuration downloads pinned Tree-sitter
sources and grammars. Database integration tests skip unless test databases are configured.
For Homebrew, supply `-DPostgreSQL_ROOT="$(brew --prefix libpq)"` if needed.

## Export a graph

```sh
./build/ingestion-debug/odysseus ingest github \
  https://github.com/OWNER/REPO --ref COMMIT_SHA --local exports/models
./build/ingestion-debug/odysseus graph-export exports/models --output exports/graph.json
```

Replace the repository URL and commit. The model directory and graph file must be
new. Keep the graph outside the model directory. Set `GITHUB_TOKEN` for authenticated
requests. GitHub ingestion downloads source; it does not create a local checkout.

Extraction is syntactic. Call sites are recorded, but their targets are unresolved.
Malformed code can produce partial models with diagnostics.

See the [reference](../docs/ingestion.md) for supported inputs, output contracts,
service configuration, and indexing commands, or the
[inspection guide](../docs/inspection.md) to use the exported graph with a checkout.
