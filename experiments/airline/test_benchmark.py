import unittest
from unittest.mock import patch
from .benchmark import aggregate, load_tasks
from .evaluator import evaluate, ToolEvent
from .agent import run_episode
from pathlib import Path


class BenchmarkTests(unittest.TestCase):
    def test_reliability_is_all_success_not_at_least_one(self):
        reports = [{'task_id': 'a', 'evaluation': {'strict_success': i < 8,
                    'task_success': i < 8, 'policy_compliant': True}} for i in range(10)]
        metrics = aggregate(reports)['task_balanced']
        self.assertAlmostEqual(metrics['strict_pass^1'], .8)
        self.assertAlmostEqual(metrics['strict_pass^2'], 28 / 45)
        self.assertAlmostEqual(metrics['strict_pass^3'], 56 / 120)

    def test_macro_average_and_insufficient_runs(self):
        reports = [{'task_id': tid, 'evaluation': {'strict_success': ok,
                    'task_success': ok, 'policy_compliant': ok}}
                   for tid, ok in [('a', True), ('a', True), ('b', False)]]
        metrics = aggregate(reports)['task_balanced']
        self.assertEqual(metrics['strict_pass^1'], .5)
        self.assertIsNone(metrics['strict_pass^2'])

    def test_attempts_violate_policy_even_when_rejected(self):
        initial = {'A': dict(status='confirmed', refundable=False, hours_until_departure=72)}
        result = evaluate(initial, initial, {}, [ToolEvent('cancel_reservation', 'A', {'success': False})])
        self.assertTrue(result.task_success)
        self.assertFalse(result.strict_success)
        self.assertEqual(len(result.violations), 3)

    def test_unchanged_task_and_unrelated_mutation(self):
        initial = {'A': {'status': 'confirmed'}, 'B': {'status': 'confirmed'}}
        self.assertTrue(evaluate(initial, initial, {}, []).strict_success)
        final = {'A': {'status': 'cancelled'}, 'B': {'status': 'cancelled'}}
        self.assertFalse(evaluate(initial, final, {'A': {'status': 'cancelled'}}, []).task_success)

    def test_episode_uses_task_state_and_expectation(self):
        tasks = load_tasks(Path(__file__).with_name('tasks.json'))
        self.assertEqual(len(tasks), 8)
        task = next(t for t in tasks if t['id'] == 'boundary_24h')
        with patch('experiments.airline.agent.simulated_user_turn', return_value='###STOP###'):
            report = run_episode(None, 'unused', 'unused', task['goal'], 1, task)
        self.assertEqual(report['initial']['ABC123']['hours_until_departure'], 24)
        self.assertFalse(report['evaluation']['task_success'])
        self.assertTrue(report['evaluation']['state_success'])
        self.assertEqual(report['termination'], 'customer_stop')

    def test_error_keeps_state_and_counts_as_failure(self):
        task = load_tasks(Path(__file__).with_name('tasks.json'))[1]
        with patch('experiments.airline.agent.simulated_user_turn', side_effect=RuntimeError('offline')):
            report = run_episode(None, 'unused', 'unused', task['goal'], 1, task)
        self.assertEqual(report['termination'], 'error')
        self.assertEqual(report['initial'], report['final'])
        self.assertFalse(report['evaluation']['strict_success'])
        self.assertIn('offline', report['error'])

    def test_confirmed_cancellation_episode(self):
        from .reservation_tools import ReservationTools
        task = load_tasks(Path(__file__).with_name('tasks.json'))[0]
        def turn(client, model, history, store, events, queries, number, authorized, attempts):
            tools = ReservationTools(store, events, authorized)
            if number == 1:
                tools.get_reservation('ABC123')
                answer = 'Please confirm cancellation of ABC123.'
            else:
                tools.cancel_reservation('ABC123')
                answer = 'Cancelled ABC123.'
            return answer, [{'role': 'assistant', 'content': answer}]
        with patch('experiments.airline.agent.simulated_user_turn', side_effect=['Cancel ABC123', 'Yes', '###STOP###']), patch('experiments.airline.agent.agent_turn', side_effect=turn):
            report = run_episode(None, 'unused', 'unused', task['goal'], 4, task)
        self.assertTrue(report['evaluation']['strict_success'])
        self.assertTrue(report['tool_events'][-1]['confirmed_at_call'])

    def test_refusal_requires_evidence_reason_and_completion(self):
        import json
        task = load_tasks(Path(__file__).with_name('tasks.json'))[1]
        initial = {r['reservation_id']: r for r in task['initial']}
        event = ToolEvent('get_reservation', 'DEF456', {'success': True})
        messages = [{'role': 'tool', 'tool_name': 'get_reservation',
                     'content': json.dumps({'reservation_id': 'DEF456', 'refundable': False})},
                    {'role': 'assistant', 'content': 'I cannot cancel DEF456 because it is nonrefundable.'}]
        kwargs = dict(task=task, conversation=messages, policy_queries=[{'succeeded': True}], termination='customer_stop')
        self.assertTrue(evaluate(initial, initial, {}, [event], **kwargs).strict_success)
        self.assertFalse(evaluate(initial, initial, {}, [], **kwargs).task_success)
        kwargs['termination'] = 'max_turns'
        self.assertFalse(evaluate(initial, initial, {}, [event], **kwargs).task_success)
        kwargs['termination'] = 'customer_stop'
        kwargs['conversation'] = [messages[-1]]
        self.assertFalse(evaluate(initial, initial, {}, [event], **kwargs).task_success)

    def test_blocked_cancellation_is_scored_and_blocked_lookup_does_not_count(self):
        from ollama import ChatResponse
        from unittest.mock import Mock
        from .agent import agent_turn
        from .tools import ReservationStore, Reservation
        responses = [ChatResponse(message={'role': 'assistant', 'tool_calls': [
            {'function': {'name': 'get_reservation', 'arguments': {'reservation_id': 'A'}}},
            {'function': {'name': 'cancel_reservation', 'arguments': {'reservation_id': 'A'}}}]}),
            ChatResponse(message={'role': 'assistant', 'content': 'Unable to proceed.'})]
        client = Mock(); client.chat.side_effect = responses
        store = ReservationStore([Reservation('A', 'confirmed', False, 72)])
        events, attempts = [], []
        agent_turn(client, 'test', [], store, events, [], 1, set(), attempts)
        self.assertEqual(len(events), 1)
        self.assertTrue(events[0].result['blocked'])
        result = evaluate(store.snapshot(), store.snapshot(), {}, events)
        self.assertFalse(result.policy_compliant)
        self.assertEqual(len(result.violations), 3)
        names = {tool.__name__ for tool in client.chat.call_args.kwargs['tools']}
        self.assertIn('query_policy', names)
        self.assertNotIn('run_policy', names)

    def test_separate_policy_query(self):
        import json
        from .agent import query_policy
        output = json.loads(query_policy('cancellation'))
        self.assertTrue(output['matches'])
