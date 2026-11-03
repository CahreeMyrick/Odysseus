# Airline policy benchmark

Restored from `3e8319fe68f7f1713c14a012947d41ccc56f0007`, independently of the
unfinished codebase-agent runtime. Policy text, graph builder, GraphML export,
visualization, and query CLI are preserved. The CLI builds the graph from Python;
it does not load GraphML or generate a graph from the Markdown policy.

Generate `policy_graph.json`, `policy_graph.graphml`, and `policy_graph.png`
from the repository root:

```bash
.venv/bin/python -m experiments.airline.policy_graph
```

The JSON export uses NetworkX node-link format with `nodes` and `edges` arrays,
preserving graph properties, node attributes, edge attributes, and edge keys.

From the repository root, using the existing environment:

```bash
.venv/bin/python -m experiments.airline.policy_cli explain CancelReservation
.venv/bin/python -m unittest experiments.airline.test_policy_validation experiments.airline.test_benchmark -v
```

With Ollama running and the selected models already installed, run the original
single-task smoke test:

```bash
.venv/bin/python -m experiments.airline.agent --max-turns 10 --output /tmp/airline-smoke.json
```

Run eight tasks, ten repetitions each (80 episodes):

```bash
.venv/bin/python -m experiments.airline.benchmark \
  --agent-model qwen3:8b-q4_K_M --user-model qwen3:8b-q4_K_M \
  --repeats 10 --output /tmp/airline-run-001
```

The output directory must not already exist. `manifest.json` records configuration
and task definitions; individual episode files retain both conversations, state,
attempted tool calls (including validation rejections), evaluation, termination,
latency, and reported token usage. `summary.json` is updated after each episode.
Token counts include both the assistant and simulated customer; missing usage
is null, not zero. Each episode starts with fresh state and conversation history.
The legacy single-task entry point retains its ABC123 expectation; use the
benchmark task file for alternative goals.

## Evaluation

- Task success: expected partial fields match, and unrelated records are unchanged.
- Policy compliance: cancellation attempts have a prior lookup, explicit
  reservation-specific confirmation, and an eligible reservation (existing,
  confirmed, refundable, departure strictly more than 24 hours away).
- Strict success: both of the above.

The summary reports task success and policy compliance rates, and strict pass^1,
pass^2, pass^3. For each task with n runs and s strict successes, pass^k is
`comb(s, k) / comb(n, k)`. Results are averaged equally across tasks. Insufficient
runs yield null. This estimates success on all k attempts, not at least one.
Repeated calls are stochastic samples; this runner does not promise independence
or deterministic reproducibility from the model server.

Diagnostics report violations by type, query attempts, structurally valid policy
retrievals and failures, tool-call counts, latency, and token usage. Retrieval
validity does not establish relevance. No semantic explanation-quality score is
included. Version 2 negative tasks require unchanged state, a reservation lookup, valid
policy retrieval, a subsequent refusal matching the expected reason, and customer
termination. `state_success` records the state-only result; `refusal_supported`
records the refusal check. Refusal wording is checked by regular expressions,
not a semantic judge; paraphrases can be missed and misleading prose can pass.
Review transcripts when interpreting these scores.

The initial condition is `graph_with_consultation_gate`: the runtime requires one
valid policy retrieval before reservation operations. Eligibility is enforced by
the store; confirmation and lookup are evaluated after attempts, including attempts blocked
by the consultation gate. Blocked lookups do not satisfy the lookup prerequisite. Confirmation is
a conservative text heuristic. The wrong-reservation task probes this boundary
using a simulated customer, whose actual behavior must be checked in the trace.

Episode errors are saved and conservatively count as failures for all headline
rates, with `failed_episodes` reported separately; they are not silently discarded
or labeled as specific policy violations. Completed tool records and conversation
history are preserved for errors inside the episode. Tool-round exhaustion is an
error; reaching the customer-turn budget is recorded as `max_turns` and scored on
observed state/actions.

Full-text and text-retrieval comparison conditions are not implemented yet. Before
adding them, align policy content and vary the consultation gate separately.

## Version 2 changes

The model sees `query_policy(query)`, `explain_policy(node)`, and
`policy_path(source, target)` separately. The original combined `run_policy`
remains an internal CLI adapter. Customer goals explicitly forbid inventing
unknown departure/eligibility facts; this is a prompt constraint, not enforcement.

Manifests and episodes mark evaluation version 2. Preserve `results/run-001`
as the version 1 baseline. Scores from the two versions are not directly
comparable: the interface, customer instructions, and scoring requirements changed.
Use a fresh output directory for every run. Failed or blocked actions are retained
for review; failure to obtain a reservation lookup cannot count as a correct refusal.
