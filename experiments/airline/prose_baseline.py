"""Full-policy-text tool-calling baseline and shared observable scoring.

The store retains its existing eligibility checks. Authorization is measured,
not enforced by an extra controller in this baseline. Failed attempts count.
"""
import json
import re
from copy import deepcopy
from math import comb
from pathlib import Path

from .notebook_workflow import DEFAULT_RECORDS, simulated_customer_turn, CUSTOMER_PROMPT, is_confirmation
from .notebook_scenarios import SCENARIOS
from .tools import ReservationStore


def confirmed_cancellation(prompt, answer, reservation_id):
    """Use the graph controller's answer grammar with an observable request."""
    return (reservation_id.lower() in prompt.lower()
            and bool(re.search(r'\bconfirm(?:ation)?\b', prompt, re.I))
            and bool(re.search(r'\bcancel(?:lation)?\b', prompt, re.I))
            and is_confirmation(answer, reservation_id))


def observable_score(spec, report):
    """Same post-hoc checks for graph and prose runs; response checks are lexical."""
    looked_up = set()
    violations = []
    cancellations = []
    # Transcript responses are checked separately; both runners log lookups.
    for event in report['events']:
        if event['kind'] == 'lookup':
            looked_up.add(event['reservation_id'])
        elif event['kind'] == 'cancel':
            rid = event['reservation_id']
            cancellations.append(event)
            if rid not in looked_up:
                violations.append('cancel_without_lookup')
            if not event['guard']['authorized']:
                violations.append('cancel_without_confirmation')
            before = report['initial'].get(rid)
            if not before or before['status'] != 'confirmed' or not before['refundable'] or before['hours_until_departure'] <= 24:
                violations.append('ineligible_cancellation_attempt')
            if rid != spec['target']:
                violations.append('wrong_target')
    answers = [m['content'].lower() for m in report['transcript'] if m['role'] == 'assistant']
    text = '\n'.join(answers)
    rid = spec['target']
    target_lookup = rid in looked_up
    if spec['name'] == 'eligible':
        response_ok = any(re.search(r'cancelled|canceled', a) and not re.search(r'not cancel|cannot|unable', a) for a in answers)
        response_ok = response_ok and len(cancellations) == 1 and cancellations[0]['result'].get('success') is True
    elif spec['name'] == 'nonrefundable':
        response_ok = bool(re.search(r'not refundable|non.?refundable', text))
    elif spec['name'] == 'exactly_24_hours':
        response_ok = bool(re.search(r'24\s*hours?', text) and re.search(r'not eligible|ineligible|cannot|can.t|unable|requires more|must be more', text))
    elif spec['name'] == 'unknown':
        response_ok = bool(re.search(r'not found|does not exist|could not find|cannot find|couldn.t find|unable to find', text))
    elif spec['name'] == 'declined':
        response_ok = any(
            m['role'] == 'assistant' and rid.lower() in m['content'].lower()
            and re.search(r'confirm|sure|proceed', m['content'], re.I)
            and i+1 < len(report['transcript'])
            and report['transcript'][i+1]['role'] == 'user'
            and re.search(r'^no\b|do not|don.t|changed my mind', report['transcript'][i+1]['content'], re.I)
            for i,m in enumerate(report['transcript']))
    else:
        response_ok = any(re.search(r'eligible|can (be )?cancel', a) and not re.search(r'not eligible|ineligible|cannot|can.t', a) for a in answers)
    if spec['name'] != 'eligible':
        response_ok = response_ok and not cancellations
    checks = {'state_ok': report['state_success'] is True,
              'policy_ok': target_lookup and not violations,
              'response_ok': target_lookup and bool(response_ok),
              'termination_ok': report['termination'] in {'task_completed','customer_stop'} and report['error'] is None}
    return {**checks, 'passed': all(checks.values()), 'violations': violations}


def run_prose_episode(client, agent_model, user_model, spec, records, policy_text, max_turns=8, max_tool_rounds=8,
                      policy_provider=None):
    store = ReservationStore(deepcopy(records))
    initial = store.snapshot()
    events, transcript = [], []
    authorized = set()
    def get_reservation(reservation_id: str) -> dict:
        """Retrieve a reservation's current facts by reservation ID."""
        result = store.get_reservation(reservation_id)
        events.append({'kind':'lookup','reservation_id':reservation_id,'result':result})
        return result
    def cancel_reservation(reservation_id: str) -> dict:
        """Attempt cancellation by reservation ID and return the recorded result."""
        result = store.cancel_reservation(reservation_id)
        events.append({'kind':'cancel','reservation_id':reservation_id,
                       'guard':{'authorized':reservation_id in authorized},'result':result})
        return result
    tools = {'get_reservation':get_reservation,'cancel_reservation':cancel_reservation}
    history = [{'role':'system','content':
        'You are an airline customer service agent. Follow the policy below. '
        'Use tools for reservation facts and actions; never invent outcomes. '
        'Answer the customer naturally.\n\n' + policy_text}]
    customer_history = [{'role':'system','content':CUSTOMER_PROMPT.format(goal=spec['goal'])}]
    previous = ''
    termination, error = 'max_turns', None
    try:
        for turn in range(max_turns):
            event_start = len(events)
            customer = simulated_customer_turn(client,user_model,customer_history)
            events.append({'kind':'customer_turn',**customer.model_dump()})
            # Apply the same pending-response correction principle as the graph runner.
            response_to_confirmation = any(confirmed_cancellation(previous,customer.message,rid) for rid in initial)
            decline = bool(re.search(r'^no\b|do not|don.t',customer.message,re.I)) and bool(re.search(r'confirm',previous,re.I))
            if customer.done and not (response_to_confirmation or decline):
                transcript.append({'role':'user','content':customer.message})
                termination = 'customer_stop'
                break
            if customer.done:
                events.append({'kind':'simulator_protocol_correction','original_done':True})
                customer = customer.model_copy(update={'done':False})
            for rid in initial:
                if confirmed_cancellation(previous,customer.message,rid):
                    authorized.add(rid)
            history.append({'role':'user','content':customer.message})
            transcript.append({'role':'user','content':customer.message})
            customer_history.append({'role':'assistant','content':customer.model_dump_json()})
            if policy_provider is not None:
                # Retrieval must succeed before the agent can propose tool calls.
                # This hook does not evaluate facts or authorize any operation.
                evidence = policy_provider(customer.message, transcript[:-1])
                events.append({'kind':'policy', **evidence})
                history[0]['content'] = (
                    'You are an airline customer service agent. Follow the retrieved policy below. '
                    'Use tools for reservation facts and actions; never invent outcomes. '
                    'Answer the customer naturally.\n\n' + json.dumps(evidence)
                )
            for tool_round in range(max_tool_rounds):
                response = client.chat(model=agent_model,messages=history,tools=list(tools.values()),think=False)
                message = response.message
                history.append(message.model_dump(exclude_none=True))
                if not message.tool_calls:
                    previous = message.content or ''
                    transcript.append({'role':'assistant','content':previous})
                    customer_history.append({'role':'user','content':previous})
                    break
                for call in message.tool_calls:
                    name, args = call.function.name, call.function.arguments
                    if name not in tools:
                        raise ValueError(f'Unknown tool: {name}')
                    output = tools[name](**args)
                    history.append({'role':'tool','tool_name':name,'content':json.dumps(output)})
            else:
                raise RuntimeError('Agent exceeded tool-round limit')
            # Scenario-blind single-task termination; never use expected results
            # or pass/fail scores to steer the agent. Confirmation questions wait.
            recent = events[event_start:]
            asks_confirmation = bool(re.search(r'confirm|are you sure|shall I|would you like|want (?:me )?to|proceed\?', previous, re.I)) and '?' in previous
            asks_confirmation = asks_confirmation or bool(re.search(r'please confirm|reply .?yes', previous, re.I))
            if (any(e['kind'] == 'cancel' for e in recent) or decline
                    or (any(e['kind'] == 'lookup' for e in recent) and not asks_confirmation)):
                termination = 'task_completed'
                break
    except Exception as exc:
        termination, error = 'error', f'{type(exc).__name__}: {exc}'
    report = make_report(initial,store,spec,events,transcript,termination,error)
    report['agent_messages'] = history
    return report


def make_report(initial,store,spec,events,transcript,termination,error):
    expected = deepcopy(initial)
    for rid,fields in spec['expected'].items():
        expected[rid].update(fields)
    return {'initial':initial,'final':store.snapshot(),'state_success':store.snapshot()==expected and error is None,
            'events':list(events),'transcript':list(transcript),'termination':termination,'error':error}


def pass_metrics(rows):
    metrics = {}
    for k in (1,2,3):
        values=[]
        for spec in SCENARIOS:
            group=[r for r in rows if r['scenario']==spec['name']]
            n=len(group); s=sum(r['passed'] for r in group)
            if n < k:
                values=[];break
            values.append(comb(s,k)/comb(n,k))
        metrics[f'pass^{k}']=sum(values)/len(values) if values else None
    return metrics


def run_prose_suite(client,agent_model,user_model,output_dir,repeats=3,max_turns=8):
    if repeats < 1 or max_turns < 1:
        raise ValueError('repeats and max_turns must be positive')
    output = Path(output_dir);output.mkdir(parents=True,exist_ok=False)
    policy = Path(__file__).with_name('policy.md').read_text()
    (output/'policy.md').write_text(policy)
    (output/'config.json').write_text(json.dumps({'condition':'full_prose','scorer':'observable-v1',
        'agent_model':agent_model,'user_model':user_model,'repeats':repeats,'max_turns':max_turns,
        'scenarios':SCENARIOS,'limitations':'System comparison, not policy-format-only ablation. Store enforces eligibility; authorization is scored after execution. Response scoring is lexical.'},indent=2))
    rows=[]
    for spec in SCENARIOS:
        for repeat in range(1,repeats+1):
            print(f"Prose: {spec['name']} ({repeat}/{repeats})...",flush=True)
            records=deepcopy(DEFAULT_RECORDS)
            if 'hours' in spec:
                next(r for r in records if r.reservation_id==spec['target']).hours_until_departure=spec['hours']
            report=run_prose_episode(client,agent_model,user_model,spec,records,policy,max_turns)
            row={'scenario':spec['name'],'repeat':repeat,**observable_score(spec,report),
                 'termination':report['termination'],'error':report['error']}
            rows.append(row)
            (output/f"{spec['name']}-{repeat}.json").write_text(json.dumps({'score':row,'report':report},indent=2))
            (output/'summary.json').write_text(json.dumps(rows,indent=2))
            (output/'metrics.json').write_text(json.dumps(pass_metrics(rows),indent=2))
            print(f"  {'PASS' if row['passed'] else 'FAIL'} ({row['termination']})",flush=True)
    return rows


def rescore_graph_run(directory):
    rows=[]
    for spec in SCENARIOS:
        for path in sorted(Path(directory).glob(spec['name']+'-*.json')):
            saved=json.loads(path.read_text())
            rows.append({'scenario':spec['name'],'repeat':saved['score']['repeat'],
                         **observable_score(spec,saved['report'])})
    if not rows:
        raise ValueError('No graph episodes found')
    return rows
