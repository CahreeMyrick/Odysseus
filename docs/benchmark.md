# Evaluation

The inspection harness compares graph-assisted and source-only runs using the
same model and task definitions. Each task/mode starts with a fresh evidence store
and conversation. It does not assume graph retrieval improves answers.

```sh
uv run odysseus-demo --output /tmp/odysseus-benchmark
uv run odysseus-inspect \
  --repo /tmp/odysseus-benchmark/repo \
  --graph /tmp/odysseus-benchmark/graph.json \
  --benchmark src/odysseus_agent/benchmarks/tasks.json --compare \
  --trace /tmp/odysseus-comparison.json
```

Requires Ollama and the selected model. Use new output paths. The graph is synthetic
and matches the fixture commit; this comparison does not test ingestion accuracy.

Automatic checks cover required answer terms, citation validity, cited file paths,
and completion. Traces include tool calls, errors, latency, and token counts when
reported. Semantic correctness and support remain unscored until human review
against each task's rubric. Keyword matches are not proof of a correct answer.

The deterministic suite checks graph validation, ambiguity, traversal direction,
stale-source rejection, source confinement, evidence delivery, and scripted model
interactions. It requires neither a model server nor external databases.

No new live-model comparison was run during repository cleanup. Historical airline
results belong to a different project and must not be used as Odysseus evidence.
