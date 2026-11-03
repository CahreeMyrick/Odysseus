# Paste this into a notebook cell after the interpretation/retrieval definitions.
import importlib
import json
from datetime import datetime
from pathlib import Path
from IPython.display import Markdown, display
import experiments.airline.prose_baseline as prose
import experiments.airline.graph_baseline as graph_baseline
importlib.reload(prose)
importlib.reload(graph_baseline)

RUN_GRAPH_NO_VALIDATOR = False  # Change to True to run.
GRAPH_NO_VALIDATOR_REPEATS = 3
CONTROLLED_RESULTS = Path('/Users/cahree/dev/Odysseus/experiments/airline/results/controlled-20261102-005111-267049')
PROSE_RESULTS = globals().get('prose_output', None)  # Or Path('/path/to/prose-run').

if RUN_GRAPH_NO_VALIDATOR:
    graph_no_validator_output = Path.cwd() / 'results' / (
        'graph-no-enforcement-' + datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    )
    graph_no_validator_rows = graph_baseline.run_graph_suite(
        interpret_customer_message, retrieve_policies, policy_graph, client,
        MODEL, USER_MODEL, graph_no_validator_output,
        repeats=GRAPH_NO_VALIDATOR_REPEATS, max_turns=8,
    )
    conditions = {
        'Controlled graph': prose.rescore_graph_run(CONTROLLED_RESULTS),
        'Graph without enforcement': graph_no_validator_rows,
    }
    if PROSE_RESULTS is not None:
        conditions['Prose'] = prose.rescore_graph_run(Path(PROSE_RESULTS))
    else:
        print('Set PROSE_RESULTS to a saved prose folder to compare all three conditions.')
    table = ['| Scenario | ' + ' | '.join(conditions) + ' |',
             '|---|' + '---|' * len(conditions)]
    for spec in prose.SCENARIOS:
        counts = []
        for rows in conditions.values():
            group = [r for r in rows if r['scenario'] == spec['name']]
            counts.append(f"{sum(r['passed'] for r in group)}/{len(group)}")
        table.append('| ' + ' | '.join([spec['name'], *counts]) + ' |')
    display(Markdown('\n'.join(table)))
    metrics = {name: prose.pass_metrics(rows) for name, rows in conditions.items()}
    comparison = {'scorer': 'observable-v1', 'metrics': metrics,
                  'controlled_source': str(CONTROLLED_RESULTS),
                  'prose_source': str(PROSE_RESULTS) if PROSE_RESULTS else None,
                  'rescored_rows': conditions}
    (graph_no_validator_output / 'comparison.json').write_text(json.dumps(comparison, indent=2))
    print(json.dumps(metrics, indent=2))
    print(f'Saved reports to {graph_no_validator_output}')
else:
    print('Set RUN_GRAPH_NO_VALIDATOR = True to run this condition.')
