import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from . import test_notebook_workflow as workflow_tests
from .notebook_scenarios import SCENARIOS, run_suite, score_scenario


class ScenarioTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        workflow_tests.WorkflowTests.setUpClass()
        cls.ns = workflow_tests.WorkflowTests.ns

    def test_full_suite_with_scripted_models(self):
        ns = self.ns
        def interpret(message, history):
            identifier = message.split()[-1]
            action = 'check_cancellation_eligibility' if message.startswith('Check') else 'cancel_reservation'
            return ns['InterpretedQuery'].model_validate({
                'goal': action, 'operation': {'intent':'retrieve' if action.startswith('check') else 'execute',
                                              'action':action,'target':'r'},
                'entities':[{'alias':'r','type':'Reservation','identifier':identifier}]})
        class Client:
            def chat(self, **kwargs):
                history = kwargs['messages']
                prompt = history[0]['content']
                spec = next(s for s in SCENARIOS if s['goal'] in prompt)
                if len(history) == 1:
                    message = ('Check ' if spec['name']=='eligibility_only' else 'Cancel ') + spec['target']
                    done = False
                elif 'Please confirm' in history[-1]['content']:
                    message = 'no' if spec['name']=='declined' else 'yes'
                    done = True  # Exercise premature-done handling too.
                else:
                    raise AssertionError('Simulator called after the scenario already reached its outcome')
                return SimpleNamespace(message=SimpleNamespace(content=json.dumps({'message':message,'done':done})))
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp)/'suite'
            rows = run_suite(interpret, ns['retrieve_policies'], ns['policy_graph'], Client(), 'fake', output, repeats=3)
            self.assertEqual(len(rows),18)
            self.assertTrue(all(r['passed'] for r in rows),rows)
            self.assertEqual(len(list(output.glob('*.json'))),20)

    def test_unchanged_state_alone_does_not_pass(self):
        report = dict(events=[],transcript=[],state_success=True,termination='customer_stop',error=None)
        for spec in SCENARIOS:
            self.assertFalse(score_scenario(spec,report)['passed'])

    def test_timeout_does_not_pass(self):
        report = dict(events=[],transcript=[],state_success=True,termination='max_turns',error=None)
        self.assertFalse(score_scenario(SCENARIOS[0],report)['termination_ok'])
