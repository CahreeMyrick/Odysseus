# Odysseus

Odysseus extracts C/C++ repository structure into a knowledge graph and provides a
read-only agent for inspecting that graph alongside source code. Answers can cite
file spans and graph records; revision checks identify stale source evidence.

## Quick start

Requires Python 3.12+, Git, and [uv](https://docs.astral.sh/uv/).
The offline example creates a small Git repository and a synthetic graph:

```sh
uv sync --locked
uv run odysseus-demo --output /tmp/odysseus-demo
uv run odysseus-inspect \
  --repo /tmp/odysseus-demo/repo \
  --graph /tmp/odysseus-demo/graph.json --inspect-only
uv run pytest
```

Choose a new demo directory for each run. No database, model, or GitHub access is
needed for this example. It validates the inspection layer, not C++ extraction.

To ask a question, start [Ollama](https://ollama.com/) and pull the model once:

```sh
ollama pull qwen3:8b-q4_K_M
uv run odysseus-inspect \
  --repo /tmp/odysseus-demo/repo \
  --graph /tmp/odysseus-demo/graph.json \
  --question 'What does helper return? Cite the implementation.'
```

Use `--model` to select another installed tool-capable model. Model runtime and
memory requirements depend on the model and hardware.

## Ingest a real repository

The C++ service fetches a GitHub snapshot, parses supported files with Tree-sitter,
and exports a graph. See [build and ingestion instructions](ingestion/README.md).
Use a local checkout at the exported commit for exact symbol-source reads.

```text
GitHub snapshot → C/C++ extraction → graph.json
                                        ↓
Local checkout + graph → inspection tools → LLM answer + evidence references
```

| Component | Location |
|---|---|
| Python inspection agent | `src/odysseus_agent/` |
| C++ ingestion and optional indexing | `ingestion/` |
| Graph and model contracts | `schemas/` |
| Python regression tests | `tests/` |
| Historical research, excluded from the package | `experiments/` |

## Scope

- Extraction covers C/C++ syntax and Markdown/text documents. Call targets and
  includes remain unresolved; impact analysis is not supported.
- The agent can search and read source but cannot edit files or execute model-selected commands.
- Citation checks verify evidence IDs and delivery, not whether every claim is supported.
- Graphs are snapshots and must be regenerated after source changes.
- PostgreSQL, Qdrant, and Neo4j are optional service paths; the local graph workflow
  does not require running them.

No performance or answer-quality advantage over source-only inspection is claimed.
See [evaluation](docs/benchmark.md) for the comparison protocol and its limits.

## Documentation

- [Architecture](docs/architecture.md)
- [Inspection tools and usage](docs/inspection.md)
- [Ingestion and indexing reference](docs/ingestion.md)
- [Development and tests](CONTRIBUTING.md)

MIT licensed. See [third-party notices](THIRD_PARTY_NOTICES.md).
