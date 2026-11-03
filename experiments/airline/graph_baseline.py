"""Mandatory graph retrieval plus the prose baseline's LLM-driven tool loop.

No runtime condition evaluator or authorization gate is inserted. The shared
store still enforces eligibility. Violations are scored after the episode.
"""
from copy import deepcopy
import json
from pathlib import Path
import re

import networkx as nx

from .notebook_workflow import DEFAULT_RECORDS
from .notebook_scenarios import SCENARIOS
from .prose_baseline import run_prose_episode, observable_score, pass_metrics, confirmed_cancellation


class GraphPolicyProvider:
    def __init__(self, interpret, retrieve, graph):
        self.interpret = interpret
        self.retrieve = retrieve
        self.graph = graph
        self.query = None

    def __call__(self, message, history):
        previous = history[-1]['content'] if history and history[-1]['role'] == 'assistant' else ''
        use_previous = False
        if self.query is not None:
            target = next(e for e in self.query.entities if e.alias == self.query.operation.target)
            if target.identifier:
                use_previous = confirmed_cancellation(previous, message, target.identifier)
                use_previous = use_previous or (
                    target.identifier.lower() in previous.lower()
                    and re.search(r'confirm', previous, re.I)
                    and message.strip().lower().rstrip('.!') in {'no', 'no thanks', 'do not cancel', "don't cancel"}
                )
        query = self.query if use_previous else self.interpret(message, history)
        # Uses the same retrieval/coverage validation as the controlled condition.
        context = self.retrieve(query, self.graph)
        self.query = query
        return {'interpreted_query': query.model_dump(), 'context': context.model_dump()}


def run_graph_suite(interpret, retrieve, graph, client, agent_model, user_model,
                    output_dir, repeats=3, max_turns=8):
    if repeats < 1 or max_turns < 1:
        raise ValueError('repeats and max_turns must be positive')
    output = Path(output_dir)
    output.mkdir(parents=True, exist_ok=False)
    (output / 'policy_graph.json').write_text(json.dumps(nx.node_link_data(graph, edges='edges'), indent=2))
    (output / 'config.json').write_text(json.dumps({
        'condition': 'graph_without_enforcement', 'scorer': 'observable-v1',
        'agent_model': agent_model, 'user_model': user_model, 'repeats': repeats,
        'max_turns': max_turns, 'scenarios': SCENARIOS,
        'mandatory_retrieval': True, 'retrieval_coverage_validation': True,
        'runtime_policy_enforcement': False,
        'limitations': 'System comparison. Same LLM tool loop as prose; includes interpretation and graph retrieval. Store eligibility protections remain. Authorization is scored, not enforced.',
    }, indent=2))
    rows = []
    for spec in SCENARIOS:
        for repeat in range(1, repeats + 1):
            print(f"Graph without enforcement: {spec['name']} ({repeat}/{repeats})...", flush=True)
            records = deepcopy(DEFAULT_RECORDS)
            if 'hours' in spec:
                next(r for r in records if r.reservation_id == spec['target']).hours_until_departure = spec['hours']
            provider = GraphPolicyProvider(interpret, retrieve, graph.copy())
            report = run_prose_episode(client, agent_model, user_model, spec, records, '',
                                       max_turns=max_turns, policy_provider=provider)
            score = observable_score(spec, report)
            # Retrieval failures produce an error and fail even if state is unchanged.
            row = {'scenario': spec['name'], 'repeat': repeat, **score,
                   'termination': report['termination'], 'error': report['error']}
            rows.append(row)
            (output / f"{spec['name']}-{repeat}.json").write_text(json.dumps({'score': row, 'report': report}, indent=2))
            (output / 'summary.json').write_text(json.dumps(rows, indent=2))
            (output / 'metrics.json').write_text(json.dumps(pass_metrics(rows), indent=2))
            print(f"  {'PASS' if row['passed'] else 'FAIL'} ({row['termination']})", flush=True)
    return rows
