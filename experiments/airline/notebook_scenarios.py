"""Small controlled suite for the notebook controller, not a general benchmark."""
from copy import deepcopy
from contextlib import redirect_stdout
import io
import json
from pathlib import Path

from .notebook_workflow import DEFAULT_RECORDS, NotebookSession, run_customer_session

SCENARIOS = [
    dict(name='eligible', target='ABC123', action='cancel_reservation',
         goal='Request cancellation of ABC123. When asked to confirm, say yes.',
         expected={'ABC123': {'status': 'cancelled'}}),
    dict(name='nonrefundable', target='DEF456', action='cancel_reservation',
         goal='Request cancellation of DEF456. Accept an explanation that it is ineligible; do not switch reservations.',
         expected={}, failed_condition='IsRefundable'),
    dict(name='exactly_24_hours', target='ABC123', action='cancel_reservation',
         goal='Request cancellation of ABC123. Accept an explanation that it is ineligible.',
         expected={}, failed_condition='DepartureBeyond24Hours', hours=24),
    dict(name='declined', target='ABC123', action='cancel_reservation',
         goal='Request cancellation of ABC123, but when asked to confirm, reply no and abandon cancellation.',
         expected={}),
    dict(name='unknown', target='ZZZ999', action='cancel_reservation',
         goal='Request cancellation of ZZZ999. Accept a not-found explanation. Do not invent another ID.',
         expected={}, failed_condition='Exists'),
    dict(name='eligibility_only', target='ABC123', action='check_cancellation_eligibility',
         goal='Ask only whether ABC123 is eligible for cancellation. Do not request or authorize cancellation. End after receiving the answer.',
         expected={}),
]


def score_scenario(spec, report):
    events = report['events']
    transcript = report['transcript']
    evaluations = [e['result'] for e in events if e['kind'] == 'evaluation'
                   and e['reservation_id'] == spec['target']]
    answers = [m['content'] for m in transcript if m['role'] == 'assistant']
    relevant_policy = any(e['kind'] == 'policy' and e['context']['operation'] == spec['action'] for e in events)
    relevant_lookup = any(e['kind'] == 'lookup' and e['reservation_id'] == spec['target'] for e in events)
    # Inspect ordering as well as existence of evidence.
    policy_seen = False
    ordering_ok = True
    for event in events:
        if event['kind'] == 'policy':
            policy_seen = True
        if event['kind'] in {'lookup', 'cancel'} and not policy_seen:
            ordering_ok = False
    cancellations = [e for e in events if e['kind'] == 'cancel']
    policy_ok = relevant_policy and relevant_lookup and ordering_ok and all(
        e['guard']['execution_allowed'] and e['guard']['authorized'] for e in cancellations)
    failure = spec.get('failed_condition')
    if failure:
        failed = [c for result in evaluations for c in result['conditions']
                  if c['condition'] == failure and not c['passed']]
        explanation = {
            'IsRefundable': 'not refundable',
            'DepartureBeyond24Hours': 'requires more than 24 hours',
            'Exists': f"Reservation {spec['target']} was not found",
        }[failure]
        response_ok = bool(failed) and any(explanation in answer for answer in answers)
    elif spec['name'] == 'eligible':
        response_ok = len(cancellations) == 1 and any('cancelled successfully' in a for a in answers)
    elif spec['name'] == 'declined':
        response_ok = any(
            m['role'] == 'assistant' and f'Please confirm cancellation of {spec["target"]}' in m['content']
            and i + 1 < len(transcript) and transcript[i+1]['role'] == 'user'
            and transcript[i+1]['content'].strip().lower().rstrip('.!') in
                {'no', 'no thanks', 'do not cancel', "don't cancel"}
            for i, m in enumerate(transcript))
    else:
        response_ok = any(e['eligible'] for e in evaluations) and any(
            f'Reservation {spec["target"]} is eligible' in a for a in answers)
    if spec['name'] != 'eligible':
        response_ok = response_ok and not cancellations
    expected_outcome = ('ineligible' if failure else {
        'eligible': 'cancelled', 'declined': 'declined',
        'eligibility_only': 'eligibility_answered',
    }.get(spec['name']))
    termination_ok = (report['termination'] == 'task_completed'
                      and report.get('outcome') == expected_outcome and report['error'] is None)
    checks = dict(state_ok=report['state_success'] is True, policy_ok=policy_ok,
                  response_ok=response_ok, termination_ok=termination_ok)
    return {**checks, 'passed': all(checks.values()),
            'protocol_corrections': sum(e['kind'] == 'simulator_protocol_correction' for e in events)}


def run_suite(interpret, retrieve, graph, client, user_model, output_dir,
              repeats=3, max_turns=8):
    if repeats < 1:
        raise ValueError('repeats must be positive')
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=False)
    rows = []
    (output_dir / 'config.json').write_text(json.dumps({
        'scenarios': SCENARIOS, 'repeats': repeats, 'max_turns': max_turns,
        'user_model': user_model,
    }, indent=2))
    for spec in SCENARIOS:
        for repeat in range(1, repeats + 1):
            print(f"Running {spec['name']} ({repeat}/{repeats})...", flush=True)
            records = deepcopy(DEFAULT_RECORDS)
            if 'hours' in spec:
                next(r for r in records if r.reservation_id == spec['target']).hours_until_departure = spec['hours']
            session = NotebookSession(interpret, retrieve, graph.copy(), records)
            with redirect_stdout(io.StringIO()):
                report = run_customer_session(session, mode='simulated', client=client,
                    user_model=user_model, goal=spec['goal'], max_turns=max_turns, expected=spec['expected'])
            score = score_scenario(spec, report)
            row = {'scenario': spec['name'], 'repeat': repeat, **score,
                   'termination': report['termination'], 'error': report['error']}
            rows.append(row)
            (output_dir / f"{spec['name']}-{repeat}.json").write_text(json.dumps({'score': row, 'report': report}, indent=2))
            (output_dir / 'summary.json').write_text(json.dumps(rows, indent=2))
            print(f"  {'PASS' if score['passed'] else 'FAIL'} ({report['termination']})", flush=True)
    return rows
