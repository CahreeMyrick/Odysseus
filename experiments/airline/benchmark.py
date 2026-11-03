"""Repeated airline episodes and task-balanced reliability estimates."""
import argparse
from collections import defaultdict
from math import comb
import json
from pathlib import Path
import time

from ollama import Client
from .agent import DEFAULT_MODEL, run_episode


class MeasuredClient:
    def __init__(self, client):
        self.client = client
        self.usage = []

    def chat(self, **kwargs):
        response = self.client.chat(**kwargs)
        self.usage.append({"model": kwargs["model"],
                           "input_tokens": response.prompt_eval_count,
                           "output_tokens": response.eval_count})
        return response


def aggregate(reports):
    grouped = defaultdict(list)
    for report in reports:
        grouped[report['task_id']].append(report)
    tasks = []
    for task_id, runs in sorted(grouped.items()):
        n = len(runs)
        successes = sum(r['evaluation']['strict_success'] for r in runs)
        metrics = {f'strict_pass^{k}': comb(successes, k) / comb(n, k) if n >= k else None
                   for k in (1, 2, 3)}
        metrics.update({f'{field}_rate': sum(r['evaluation'][field] for r in runs) / n
                        for field in ('task_success', 'policy_compliant')})
        tasks.append({'task_id': task_id, 'runs': n, **metrics})
    names = ['task_success_rate', 'policy_compliant_rate', 'strict_pass^1', 'strict_pass^2', 'strict_pass^3']
    macro = {name: sum(t[name] for t in tasks) / len(tasks)
             if tasks and all(t[name] is not None for t in tasks) else None for name in names}
    violations = defaultdict(int)
    for report in reports:
        for violation in report['evaluation'].get('violations', []):
            violations[violation.split(':', 1)[0]] += 1
    usage = [entry for report in reports for entry in report.get('usage', [])]
    tokens = {key: sum(entry[key] for entry in usage)
              if usage and all(isinstance(entry.get(key), int) for entry in usage) else None
              for key in ('input_tokens', 'output_tokens')}
    queries = [query for report in reports for query in report.get('policy_retrieval', {}).get('queries', [])]
    diagnostics = {'violations_by_type': dict(violations),
                   'policy_query_attempts': len(queries),
                   'valid_policy_retrievals': sum(q['succeeded'] for q in queries),
                   'policy_retrieval_failures': sum(not q['succeeded'] for q in queries),
                   'tool_calls': sum(r.get('tool_call_count', 0) for r in reports),
                   'total_elapsed_seconds': sum(r.get('elapsed_seconds', 0) for r in reports),
                   **tokens}
    return {'tasks': tasks, 'task_balanced': macro, 'diagnostics': diagnostics,
            'failed_episodes': sum(r.get('error') is not None for r in reports)}


def load_tasks(path):
    tasks = json.loads(path.read_text())
    if not isinstance(tasks, list) or not tasks:
        raise ValueError('Tasks must be a nonempty list')
    ids = set()
    for task in tasks:
        if not isinstance(task['id'], str) or not task['id'] or task['id'] in ids:
            raise ValueError('Task IDs must be unique nonempty strings')
        ids.add(task['id'])
        if not isinstance(task['goal'], str) or not task['goal'].strip():
            raise ValueError('Each task needs a customer goal')
        if not task['expected'] and 'refusal' not in task:
            raise ValueError('Unchanged-state tasks require a refusal specification')
        if 'refusal' in task:
            spec = task['refusal']
            if not isinstance(spec, dict) or not isinstance(spec.get('reservation_id'), str) or not spec['reservation_id']:
                raise ValueError('Refusal needs a reservation ID')
            if spec.get('reason') not in {'nonrefundable', 'departure', 'already_cancelled', 'missing', 'authorization'}:
                raise ValueError('Unknown refusal reason')
        records = task['initial']
        if not records or len({r['reservation_id'] for r in records}) != len(records):
            raise ValueError('Initial reservation IDs must be unique')
        if not isinstance(task['expected'], dict):
            raise ValueError('Expected state must be a mapping')
    return tasks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tasks', type=Path, default=Path(__file__).with_name('tasks.json'))
    parser.add_argument('--repeats', type=int, default=10)
    parser.add_argument('--max-turns', type=int, default=10)
    parser.add_argument('--agent-model', default=DEFAULT_MODEL)
    parser.add_argument('--user-model', default=DEFAULT_MODEL)
    parser.add_argument('--output', type=Path, required=True, help='New results directory')
    args = parser.parse_args()
    if args.repeats < 1 or args.max_turns < 1:
        parser.error('Repeats and max-turns must be positive')
    tasks = load_tasks(args.tasks)
    args.output.mkdir(parents=True, exist_ok=False)
    config = {k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()}
    (args.output / 'manifest.json').write_text(json.dumps({'config': config, 'tasks': tasks,
        'evaluation_version': 2, 'policy_interface': 'separate_query_explain_path',
        'condition': 'graph_with_consultation_gate', 'restored_from': '3e8319fe68f7f1713c14a012947d41ccc56f0007'}, indent=2))
    reports = []
    for task in tasks:
        for repeat in range(args.repeats):
            client = MeasuredClient(Client())
            started = time.perf_counter()
            try:
                report = run_episode(client, args.agent_model, args.user_model, task['goal'], args.max_turns, task)
            except Exception as exc:
                report = {'error': f'{type(exc).__name__}: {exc}',
                          'termination': 'error', 'evaluation': {'task_success': False,
                          'policy_compliant': False, 'strict_success': False, 'violations': []}}
            report.update(task_id=task['id'], repeat=repeat + 1,
                          condition='graph_with_consultation_gate',
                          elapsed_seconds=time.perf_counter() - started, usage=client.usage)
            reports.append(report)
            (args.output / f'episode-{len(reports):04d}.json').write_text(json.dumps(report, indent=2))
            summary = aggregate(reports)
            summary.update(evaluation_version=2, completed_episodes=len(reports),
                           planned_episodes=len(tasks) * args.repeats,
                           complete=len(reports) == len(tasks) * args.repeats)
            (args.output / 'summary.json').write_text(json.dumps(summary, indent=2))
    print(json.dumps(aggregate(reports), indent=2))


if __name__ == '__main__':
    main()
