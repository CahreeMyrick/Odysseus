# Contributing

## Python

Requires Python 3.12+, Git, and uv.

```sh
uv sync --locked
uv run ruff check src tests
uv run ruff format --check src tests
uv run pytest
uv build
```

The package lives in `src/odysseus_agent/`; deterministic tests live in `tests/`.
Keep model calls behind the client interface so tests do not require Ollama.
Include regression tests when changing tool boundaries, evidence handling, or graph contracts.

## C++

Follow [build instructions](ingestion/README.md), then run:

```sh
cmake --build build/ingestion-debug -j 4
ctest --test-dir build/ingestion-debug --output-on-failure
```

PostgreSQL integration tests require isolated test databases and explicit environment
variables. Their default skipped status is not evidence of integration coverage.

## Changes

Describe the problem, resulting behavior, and validation in pull requests. Preserve
schema compatibility or document migration requirements. Do not introduce unresolved
call-graph or answer-quality claims.
Never commit credentials, private repository exports, or generated traces.
