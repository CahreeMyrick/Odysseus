"""Regression tests for the notebook's customer simulation protocol."""
import json
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from pydantic import ValidationError
from .notebook_workflow import (CustomerTurn, NotebookSession, is_confirmation,
                               run_customer_session)


class WorkflowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.ns = {'sys': sys}
        notebook = json.loads(Path(__file__).with_name('notebook.ipynb').read_text())
        for cell in notebook['cells']:
            source = ''.join(cell['source'])
            if cell['cell_type'] == 'code':
                compile(source, '<notebook>', 'exec')
                if source.startswith(('from typing import', 'from pathlib import Path')):
                    exec(source, cls.ns)

    def session(self, action='cancel_reservation'):
        query = self.ns['InterpretedQuery'].model_validate({
            'goal': action, 'operation': {'intent': 'execute' if action == 'cancel_reservation' else 'retrieve',
                                         'action': action, 'target': 'r'},
            'entities': [{'alias': 'r', 'type': 'Reservation', 'identifier': 'ABC123'}]})
        return NotebookSession(lambda message, history: query,
                               self.ns['retrieve_policies'], self.ns['policy_graph'].copy())

    def test_confirmation_grammar(self):
        for message in ['yes', 'Yes, please cancel ABC123.', 'yes please', 'confirm ABC123', 'Yes, please cancel it!']:
            self.assertTrue(is_confirmation(message, 'ABC123'), message)
        for message in ['yes###STOP###', 'yes but not now', 'cancel DEF456', 'yes, cancel DEF456 instead', 'no', 'yes, do not cancel ABC123']:
            self.assertFalse(is_confirmation(message, 'ABC123'), message)

    def test_confirmation_and_fresh_facts(self):
        session = self.session()
        self.assertIn('Please confirm', session.reply('cancel ABC123'))
        self.assertEqual(session.store.snapshot(), session.initial)
        session.store._records['ABC123'].hours_until_departure = 24
        self.assertIn('not eligible', session.reply('yes'))
        self.assertTrue(session.completed)
        self.assertEqual(session.outcome, "ineligible")

    def test_structured_simulation_stops_after_success(self):
        turns = iter([{'message': 'Cancel ABC123', 'done': False},
                      {'message': 'Yes, please cancel ABC123.', 'done': False}])
        class Client:
            def chat(self, **kwargs):
                assert 'format' in kwargs
                return SimpleNamespace(message=SimpleNamespace(content=json.dumps(next(turns))))
        report = run_customer_session(self.session(), mode='simulated', client=Client(), user_model='fake',
                                      expected={'ABC123': {'status': 'cancelled'}})
        self.assertEqual(report['termination'], 'task_completed')
        self.assertTrue(report['state_success'])
        self.assertEqual(report['cancellation_attempts'], 1)

    def test_decline_and_no_attempt_report(self):
        session = self.session()
        session.reply('cancel ABC123')
        session.reply('no')
        self.assertEqual(session.store.snapshot(), session.initial)
        self.assertIsNone(session.report()['cancellation_guards_passed'])

    def test_malformed_protocol_fails(self):
        with self.assertRaises(ValidationError):
            CustomerTurn(message='yes###STOP###', done=False)
        with self.assertRaises(ValidationError):
            CustomerTurn(message='yes', done='false')

    def test_confirmation_with_premature_done_is_processed(self):
        class Client:
            def chat(self, **kwargs):
                return SimpleNamespace(message=SimpleNamespace(content='{"message":"yes","done":true}'))
        session = self.session()
        session.reply('cancel ABC123')
        report = run_customer_session(session, mode='simulated', client=Client(), user_model='fake',
                                      expected={'ABC123': {'status':'cancelled'}})
        self.assertEqual(report['termination'], 'task_completed')
        self.assertTrue(report['state_success'])
        self.assertEqual(report['cancellation_attempts'], 1)
        self.assertTrue(any(e['kind'] == 'simulator_protocol_correction'
                            for e in report['events']))

    def test_done_without_pending_confirmation_does_not_cancel(self):
        class Client:
            def chat(self, **kwargs):
                return SimpleNamespace(message=SimpleNamespace(content='{"message":"yes","done":true}'))
        report = run_customer_session(self.session(), mode='simulated', client=Client(), user_model='fake',
                                      expected={'ABC123': {'status':'cancelled'}})
        self.assertEqual(report['termination'], 'customer_stop')
        self.assertFalse(report['state_success'])
        self.assertEqual(report['cancellation_attempts'], 0)

    def test_done_with_refusal_preserves_state(self):
        class Client:
            def chat(self, **kwargs):
                return SimpleNamespace(message=SimpleNamespace(content='{"message":"no","done":true}'))
        session = self.session()
        session.reply('cancel ABC123')
        report = run_customer_session(session, mode='simulated', client=Client(), user_model='fake')
        self.assertEqual(report['termination'], 'task_completed')
        self.assertEqual(report['outcome'], 'declined')
        self.assertEqual(report['initial'], report['final'])

    def test_premature_done_confirmation_still_checks_eligibility(self):
        class Client:
            def chat(self, **kwargs):
                return SimpleNamespace(message=SimpleNamespace(content='{"message":"yes","done":true}'))
        session = self.session()
        session.reply('cancel ABC123')
        session.store._records['ABC123'].hours_until_departure = 24
        report = run_customer_session(session, mode='simulated', client=Client(), user_model='fake', max_turns=1)
        self.assertEqual(report['cancellation_attempts'], 0)
        self.assertEqual(report['final']['ABC123']['status'], 'confirmed')

    def test_manual_mode(self):
        turns = iter(['cancel ABC123', 'yes please'])
        report = run_customer_session(self.session(), input_fn=lambda _: next(turns))
        self.assertEqual(report['termination'], 'task_completed')

    def test_eligibility_does_not_authorize(self):
        session = self.session('check_cancellation_eligibility')
        session.reply('Can I cancel ABC123?')
        self.assertIsNone(session.pending)
        self.assertEqual(session.store.snapshot(), session.initial)


if __name__ == '__main__':
    unittest.main()
