import unittest
from . import test_notebook_workflow as wf_tests
from .test_prose_baseline import Client, Message
from .graph_baseline import GraphPolicyProvider
from .prose_baseline import run_prose_episode, observable_score
from .notebook_workflow import DEFAULT_RECORDS
from .notebook_scenarios import SCENARIOS


class GraphBaselineTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        wf_tests.WorkflowTests.setUpClass()
        cls.ns = wf_tests.WorkflowTests.ns

    def provider(self):
        self.interpret_calls = 0
        def interpret(message, history):
            self.interpret_calls += 1
            return self.ns['InterpretedQuery'].model_validate({
                'goal':'cancel', 'operation':{'intent':'execute','action':'cancel_reservation','target':'r'},
                'entities':[{'alias':'r','type':'Reservation','identifier':'ABC123'}]})
        return GraphPolicyProvider(interpret,self.ns['retrieve_policies'],self.ns['policy_graph'].copy())

    def test_retrieval_precedes_tools_but_does_not_enforce_authorization(self):
        client=Client([Message(tool='get_reservation'),Message(tool='cancel_reservation'),Message('ABC123 cancelled successfully.')],
                      [{'message':'Cancel ABC123','done':False}])
        report=run_prose_episode(client,'a','u',SCENARIOS[0],DEFAULT_RECORDS,'',policy_provider=self.provider())
        kinds=[e['kind'] for e in report['events']]
        self.assertLess(kinds.index('policy'),kinds.index('lookup'))
        self.assertIn('IsRefundable',client.prompt)
        self.assertTrue(report['state_success'])
        self.assertFalse(observable_score(SCENARIOS[0],report)['passed'])
        self.assertIn('cancel_without_confirmation',observable_score(SCENARIOS[0],report)['violations'])

    def test_confirmation_reuses_query_and_retrieves_again(self):
        client=Client([Message(tool='get_reservation'),Message('Please confirm cancellation of ABC123.'),
                       Message(tool='cancel_reservation'),Message('ABC123 cancelled successfully.')],
                      [{'message':'Cancel ABC123','done':False},{'message':'yes','done':False}])
        report=run_prose_episode(client,'a','u',SCENARIOS[0],DEFAULT_RECORDS,'',policy_provider=self.provider())
        self.assertTrue(observable_score(SCENARIOS[0],report)['passed'])
        self.assertEqual(self.interpret_calls,1)
        self.assertEqual(sum(e['kind']=='policy' for e in report['events']),2)

    def test_incomplete_retrieval_blocks_agent(self):
        provider=self.provider()
        provider.graph.remove_edge('CancelReservation','IsRefundable')
        client=Client([], [{'message':'Cancel ABC123','done':False}])
        report=run_prose_episode(client,'a','u',SCENARIOS[0],DEFAULT_RECORDS,'',policy_provider=provider)
        self.assertEqual(report['termination'],'error')
        self.assertIn('PolicyRetrievalError',report['error'])
        self.assertFalse(any(e['kind'] in {'lookup','cancel'} for e in report['events']))
        self.assertFalse(hasattr(client,'prompt'))
