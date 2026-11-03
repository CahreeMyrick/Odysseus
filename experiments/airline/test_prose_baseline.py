import json
import unittest
from types import SimpleNamespace
from .prose_baseline import run_prose_episode, observable_score, pass_metrics
from .notebook_scenarios import SCENARIOS
from .notebook_workflow import DEFAULT_RECORDS


class Message:
    def __init__(self, content='', tool=None):
        self.content = content
        self.tool_calls = [] if tool is None else [SimpleNamespace(function=SimpleNamespace(name=tool,arguments={'reservation_id':'ABC123'}))]
    def model_dump(self, **kwargs):
        return {'role':'assistant','content':self.content}


class Client:
    def __init__(self, messages, customers):
        self.messages=iter(messages);self.customers=iter(customers)
    def chat(self, **kwargs):
        if 'format' in kwargs:
            return SimpleNamespace(message=SimpleNamespace(content=json.dumps(next(self.customers))))
        self.prompt = kwargs['messages'][0]['content']
        self.tools = [t.__name__ for t in kwargs['tools']]
        return SimpleNamespace(message=next(self.messages))


class ProseTests(unittest.TestCase):
    def test_confirmed_cancellation(self):
        client=Client([Message(tool='get_reservation'),Message('Please confirm cancellation of ABC123.'),
                       Message(tool='cancel_reservation'),Message('ABC123 was cancelled successfully.')],
                      [{'message':'Cancel ABC123','done':False},{'message':'yes','done':True}])
        report=run_prose_episode(client,'agent','user',SCENARIOS[0],DEFAULT_RECORDS,'TEST POLICY')
        self.assertTrue(observable_score(SCENARIOS[0],report)['passed'])
        self.assertIn('TEST POLICY',client.prompt)
        self.assertEqual(set(client.tools),{'get_reservation','cancel_reservation'})

    def test_unauthorized_cancellation_fails_despite_changed_state(self):
        client=Client([Message(tool='get_reservation'),Message(tool='cancel_reservation'),
                       Message('ABC123 cancelled successfully.')],[{'message':'Cancel ABC123','done':False}])
        report=run_prose_episode(client,'agent','user',SCENARIOS[0],DEFAULT_RECORDS,'policy')
        self.assertTrue(report['state_success'])
        score=observable_score(SCENARIOS[0],report)
        self.assertFalse(score['passed'])
        self.assertIn('cancel_without_confirmation',score['violations'])

    def test_eligibility_only(self):
        client=Client([Message(tool='get_reservation'),Message('ABC123 is eligible for cancellation.')],
                      [{'message':'Can I cancel ABC123?','done':False}])
        report=run_prose_episode(client,'agent','user',SCENARIOS[-1],DEFAULT_RECORDS,'policy')
        self.assertTrue(observable_score(SCENARIOS[-1],report)['passed'])
        self.assertEqual(report['initial'],report['final'])

    def test_error_counts_as_failure(self):
        client=Client([], [{'message':'Cancel ABC123','done':False}])
        report=run_prose_episode(client,'agent','user',SCENARIOS[0],DEFAULT_RECORDS,'policy')
        self.assertEqual(report['termination'],'error')
        self.assertFalse(observable_score(SCENARIOS[0],report)['passed'])

    def test_pass_metrics(self):
        rows=[{'scenario':s['name'],'passed':True} for s in SCENARIOS for _ in range(3)]
        self.assertEqual(pass_metrics(rows),{'pass^1':1.0,'pass^2':1.0,'pass^3':1.0})
