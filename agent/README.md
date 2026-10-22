# Codebase inspection agent

A read-only Python agent that combines an Odysseus graph export with a local
checkout. It uses NetworkX for graph queries and Ollama for the conversation.
The old airline benchmark has been replaced; its history remains in Git.

## Setup and inspection

From the Odysseus repository root:

```bash
uv sync --locked
.venv/bin/python -m agent \
  --repo /path/to/the/exported/repository \
  --graph /path/to/graph.json \
  --inspect-only
```

`--repo` is the checkout whose code the graph describes, not necessarily the
Odysseus directory. `--inspect-only` needs no model server and reports graph
capabilities, partial parses, revision agreement, and working-tree changes.
For an export with multiple repositories/revisions, select exact values with
`--source-uri URI --revision COMMIT`.

Start Ollama with a tool-capable model available, then run:

```bash
.venv/bin/python -m agent \
  --repo /path/to/the/exported/repository \
  --graph /path/to/graph.json \
  --model qwen3:4b-instruct-16k \
  --question 'Where is this behavior implemented? Cite the relevant source.' \
  --trace /tmp/inspection-session.json
```

The default model is `qwen3:4b-instruct-16k`; override it with `--model` for your
installed model. Nothing downloads a model automatically. Omit `--question`
for an interactive session; enter `exit` or `quit` to finish. The graph and
conversation are retained across questions. The graph does not automatically
refresh when files change; start a new session after regenerating the export.

Trace files contain questions, answers, evidence, tool calls, timing, and reported
token usage. They can contain source code. They are written only with `--trace`,
and an existing output file is never overwritten. The parent directory must exist.

## Boundaries and evidence

- Graph schema v1 is validated before loading into `MultiDiGraph`. Unknown node
  types, invalid relationship endpoints/types, duplicate IDs, and inconsistent
  selected-file provenance are rejected.
- Only one repository/revision is selected. Symbols are indexed by ID, name,
  qualified name, and path. Same-named symbols produce candidates, not guesses.
- Available graph relationships are `HAS_REVISION`, `CONTAINS`, `DEFINES`, and
  `HAS_CALL_SITE`. Resolved calls and impact analysis are explicitly unsupported.
- Graph records are historical evidence. Source tools read current working-tree
  files and record content hashes. Exact symbol-span reads require the checkout
  HEAD to match the graph commit and the file bytes to match that commit. Otherwise
  the tool returns `stale`; ordinary file reads/search remain available.
- Traversals are bounded and preserve relationship direction. Path finding requires
  explicit relationship types. No path within a bound is not proof of no dependency.
- Reads stay inside the selected root, reject paths through `.git`, and reject
  symlinks that resolve outside the root. Source access is UTF-8 only and size-limited.
  Search excludes build/cache directories, skips unreadable/binary/oversized files,
  and reports search limits. It is literal, case-insensitive search, not embeddings.
- The model has no editing, shell execution, or test-execution tools.
- Answers use `[ev-N]` references. The application checks that cited evidence was
  actually delivered to the model, and renders source references. This checks
  citation identity, not whether the evidence supports every natural-language claim.
  Citation warnings do not silently rewrite or certify an answer.

## Tools

`repository_status`, `find_symbols`, `describe_symbol`, `get_neighbors`,
`find_path`, `read_symbol_source`, `read_file`, and `search_source`.

`--source-only` disables graph tools and does not load the graph. It retains the
same source tools and model instructions, providing an experimental baseline.

## Tests and benchmark

Tests use isolated Git fixtures and a scripted model client. No Ollama server,
GitHub access, or database is required:

```bash
.venv/bin/python -m unittest discover -s agent/tests -t . -v
```

Prepare a small standalone repository and a synthetic graph matching its commit:

```bash
.venv/bin/python -m agent.benchmarks.prepare --output /tmp/odysseus-inspection-demo
```

Then compare graph-assisted and source-only runs with the same model and tasks:

```bash
.venv/bin/python -m agent \
  --repo /tmp/odysseus-inspection-demo/repo \
  --graph /tmp/odysseus-inspection-demo/graph.json \
  --benchmark agent/benchmarks/tasks.json --compare \
  --trace /tmp/inspection-comparison.json
```

The example tasks apply to this fixture, not arbitrary repositories. Add task
files with `id`, `question`, `review_rubric`, optional `answer_contains`, and
optional `expected_evidence_paths`. Each task/mode gets a fresh agent and evidence
store. Model responses are not deterministic; repeat comparisons before drawing
conclusions. No knowledge-graph advantage is assumed or claimed by the harness.

The evaluator checks answer terms, citations, cited file coverage, and completion;
it records tool counts, errors, tokens (when reported), and latency. These are
mechanical checks. Semantic correctness/support remain `null` pending human review
against the rubric. Merely using the graph is not scored as success.

## Code map

- `graph_store.py`, `repository.py`: graph and checkout context.
- `retrieval.py`: lexical ranking, bounded BFS, typed directed shortest paths.
- `tools.py`, `schemas.py`: model-facing tools, argument checks, result envelopes.
- `evidence.py`, `tracing.py`: citations and observable session records.
- `runtime.py`, `prompts.py`, `config.py`: model boundary and conversation loop.
- `evaluation.py`, `benchmarks/`: experimental comparison and review criteria.

The synthetic fixture graph tests the inspection layer independently of C++
extraction. Real ingestion outputs can be converted with the existing
`odysseus graph-export` command and supplied unchanged to this agent.
