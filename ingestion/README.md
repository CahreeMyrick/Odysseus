## Ingestion Service

The objective of this service is to process user data and update associated databases.

### How does ingestion work

```mermaid
flowchart LR
    A[Data] --> B[Ingestion Service]
    B --> C[(PostgreSQL)]
    B --> D[(Qdrant)]
    B --> E[(Neo4j)]

    style C fill:#336791,color:#fff
```

(We have currently only integrated **PostgreSQL**)

When a user references a datasource such a `github repo` or `document corpus`



### How do we populate associated databases 

### How to use it

Build dependencies: C++17, CMake, nlohmann_json, PostgreSQL libpq, and libcurl.

```sh
cmake -S ingestion -B build/ingestion
cmake --build build/ingestion --parallel
ctest --test-dir build/ingestion --output-on-failure
./build/ingestion/odysseus ingest github https://github.com/OWNER/REPO
./build/ingestion/odysseus ingest github https://github.com/OWNER/REPO --ref main
```

`--ref` accepts a branch, tag, or commit; omission selects the default branch.
The client resolves the revision to a commit before listing and fetching blobs.
Set `GITHUB_TOKEN` in the environment for private repositories or authenticated
rate limits; it needs read access to repository contents. Tokens and API error
bodies are not printed. Add `--postgres` with `ODYSSEUS_DATABASE_URL` configured
to persist results; otherwise they print.

Use `--mock` with `https://github.com/example/mock-repo` for the built-in fixture
without HTTP requests. `--mock` and `--ref` cannot be combined.

Supported extensions are `.c`, `.h`, `.cpp`, `.cc`, `.cxx`, `.hpp`, `.md`, and
`.txt`. Other file paths are counted as skipped without downloading their blobs.
Symlinks and submodules are not followed or counted. Supported content must be
UTF-8; binary content and processing/storage failures stop ingestion.

Current retrieval limits: github.com HTTPS URLs only; truncated tree listings
fail explicitly; moved repositories require their current URL; HTTP errors and
rate limits fail without automatic retries. Empty GitHub repositories report an
error. Supported files are limited to 8 MiB, HTTP responses to 16 MiB, and total
fetched text to 64 MiB. Retrieval is sequential and materializes a snapshot before
processing. Git LFS objects are not downloaded; pointer files remain ordinary
contents. `.git` suffixes and trailing slashes are accepted.


### Work to be done / Things to be added



## Source adapter registration

`AdapterFactory` maps source types to adapter creator functions. Application setup
registers GitHub with its client; the factory and pipeline do not depend on a
GitHub client. Each creator must return a fresh adapter, and captured dependencies
must outlive both the factory and its adapters. Duplicate registrations, empty
creators, and null adapter results are rejected.

Local files/directories, object storage, and databases are future source extension
points, not implemented adapters. Only GitHub is registered today.

## PostgreSQL verification

The deterministic storage test verifies upserts, Unicode/empty content, structured
models, rollback of new and updated artifacts, and recovery after a failed write.
The opt-in live test fetches `octocat/Spoon-Knife`, pins its commit, ingests it twice,
and compares database content and provenance with the fetched snapshot.

Use separate, newly created test databases, not your normal ingestion database:

```sh
createdb odysseus_storage_test
createdb odysseus_github_test
ODYSSEUS_TEST_DATABASE_URL='dbname=odysseus_storage_test' \
ODYSSEUS_GITHUB_TEST_DATABASE_URL='dbname=odysseus_github_test' \
  ctest --test-dir build/ingestion --output-on-failure
```

Both tests refuse a database that already contains the ingestion schema and leave
records available for inspection. Use fresh databases for subsequent runs. With
no corresponding environment variable, each integration test is skipped. Only
the live test requires GitHub network access; `GITHUB_TOKEN` is optional.

## Vector and graph indexing

PostgreSQL ingestion now saves two durable tasks per artifact in the same
transaction: `vector` and `graph`. Repeat ingestion with unchanged content/model
keeps existing task status. Changed input increments the task generation and
requeues both targets. Workers claim tasks with a lease; failed tasks become
eligible after 60 seconds, and expired leases can be reclaimed after a crash.
The current retry policy has no attempt limit. Queue creation is additive for
existing databases; re-ingest existing artifacts to enqueue their projections.

Start the local development services (Docker required):

```sh
docker compose -f ingestion/compose.indexing.yml up -d
docker compose -f ingestion/compose.indexing.yml exec ollama ollama pull nomic-embed-text
```

The Compose stack uses dedicated volumes and localhost-only ports: Qdrant on
16333, Neo4j HTTP on 17474, and Ollama on 11435. Neo4j authentication is disabled
in this local development stack; do not expose it publicly.

After ingestion with `--postgres`, run:

```sh
./build/ingestion/odysseus-index all
# Or process one destination:
./build/ingestion/odysseus-index vector
./build/ingestion/odysseus-index graph
```

The command reads `ODYSSEUS_DATABASE_URL`, processes up to 100 ready tasks per
target, then exits. Run it again to drain more tasks or retry failures. A backend
failure stops that target for this invocation but does not stop the other target.
There is no continuously running scheduler yet.

Vector tasks split nonempty text into UTF-8-safe chunks of at most 1200 bytes,
request local Ollama embeddings, and upsert them into the Qdrant collection
`odysseus_nomic_v1`. Empty files complete without vectors. The default embedding
model is `nomic-embed-text`, using its `search_document: ` prefix; use the matching
`search_query: ` prefix for future search queries. This is basic size-based
chunking, not syntax-aware chunking.

Graph tasks merge repository, revision, and file nodes connected by
`HAS_REVISION` and `CONTAINS`. They do not yet extract code symbols, document
entities, or semantic relationships. Constraints prevent duplicate nodes on retry.

Configuration overrides:

- `QDRANT_URL`, `QDRANT_COLLECTION`, `QDRANT_API_KEY`
- `OLLAMA_URL`, `ODYSSEUS_EMBEDDING_MODEL`
- `NEO4J_URL` (HTTP endpoint), `NEO4J_DATABASE`, `NEO4J_USER`, `NEO4J_PASSWORD`

Keep one embedding model per collection. Changing a model requires a separate
collection and explicit reindexing; switching environment variables does not
reset completed tasks. Automatic model migrations are not implemented.

Projections use task ID + generation as stable keys. Retrying the same generation
is idempotent. Older generations are retained, so a delayed worker cannot overwrite
a newer generation. Consumers must select the generation recorded as completed
in PostgreSQL; raw database searches may include partial or old generations.
Automatic cleanup and a query layer that enforces this are future work.

Inspect progress without exposing artifact contents:

```sql
SELECT target, status, count(*)
FROM odysseus_ingestion.indexing_tasks
GROUP BY target, status ORDER BY target, status;
```

Workers renew leases during processing and reject completion by expired or
replaced claims. Delivery is at least once, not an atomic transaction across all
three databases. Stored error messages are generic to avoid persisting credentials
or source data from backend diagnostics.

To stop the local services while retaining data:

```sh
docker compose -f ingestion/compose.indexing.yml stop
```

API references: [Qdrant REST](https://api.qdrant.tech/),
[Neo4j Query API](https://neo4j.com/docs/query-api/current/query/), and
[Ollama embeddings](https://docs.ollama.com/api/embed).
Use a separate Qdrant collection and Neo4j database for each PostgreSQL ingestion
database, since task identifiers are local to that database.
