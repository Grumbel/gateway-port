"""Asset-independent tests of the reviewed walkthrough-to-native-input pipeline."""

import pathlib
import subprocess
import sys
import tempfile
import unittest

TOOLS = pathlib.Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))

from run_walkthrough import compile_steps, convert_text, parse_script
from story_scenarios import schedule


class WalkthroughTests(unittest.TestCase):
    def test_first_two_commands_match_existing_native_puzzle_scenario(self):
        script = parse_script('COMMAND open drawer\nCOMMAND look\n')
        replay = compile_steps(script)
        self.assertEqual(replay.keys, schedule('puzzle', ('open drawer', 'look')))
        self.assertEqual(replay.duration_ms, 24000)

    def test_more_than_forty_eight_commands_cross_original_time_limit(self):
        script = parse_script('COMMAND look\n' * 75)
        replay = compile_steps(script)
        self.assertEqual(len(replay.actions), 75)
        self.assertGreater(replay.duration_ms, 180000)
        self.assertEqual(len(replay.keys), len(schedule('look')) + 3 + 75 * 5)
        self.assertTrue(all(a[0] < b[0] for a, b in zip(replay.keys, replay.keys[1:])))

    def test_beach_casino_fixture_preserves_established_native_route(self):
        fixture = pathlib.Path(__file__).parent / 'fixtures/gateway-beach-casino.walk'
        steps = parse_script(fixture.read_text(encoding='utf-8'))
        established = compile_steps(steps, through='casino_arrival')
        route = tuple(s.argument if s.operation == 'COMMAND' else '@space'
                      for s in established.actions)
        self.assertEqual(len(route), 39)
        self.assertEqual(established.keys, schedule('puzzle', route))
        self.assertEqual(established.assertions[:2], [(0xd3, 0xf6, 42),
                                                      (0x2a9, 0xd3, 43)])
        extended = compile_steps(steps)
        self.assertEqual(len(extended.actions), 55)
        self.assertGreater(extended.duration_ms, 180000)

    def test_pv_office_fixture_menu_and_named_location_checkpoints(self):
        fixture = pathlib.Path(__file__).parent / 'fixtures/gateway-pv-office.walk'
        replay = compile_steps(parse_script(fixture.read_text(encoding='utf-8')))
        self.assertEqual(len(replay.actions), 9)
        self.assertEqual(replay.duration_ms, 57000)
        self.assertEqual([(ms, key) for ms, key in replay.keys
                          if 24000 <= ms <= 36000],
                         [(24000, ord('1')), (27000, ord('N')),
                          (30000, ord('N')), (33000, ord('0')),
                          (36000, ord('0'))])
        self.assertEqual(len(replay.checkpoints), 2)
        self.assertEqual([c.label for c in replay.checkpoints],
                         ['pv_messages_read', 'office_book_exchange'])
        self.assertEqual([(obj, loc) for obj, loc, _ in replay.checkpoints[0].assertions],
                         [(0x126, 0xd3), (0xd3, 0xee)])
        self.assertEqual([(obj, loc) for obj, loc, _ in replay.checkpoints[1].assertions],
                         [(0xd3, 0xec), (0x123, 0), (0x130, 0xd3)])

    def test_orientation_fixture_asserts_blue_badge_after_native_waits(self):
        fixture = pathlib.Path(__file__).parent / 'fixtures/gateway-orientation.walk'
        replay = compile_steps(parse_script(fixture.read_text(encoding='utf-8')))
        self.assertEqual(len(replay.actions), 18)
        self.assertEqual(replay.duration_ms, 84000)
        self.assertEqual([c.label for c in replay.checkpoints],
                         ['pv_messages_read', 'office_book_exchange', 'room_t20',
                          'class_badge_awarded'])
        self.assertEqual([(obj, loc) for obj, loc, _ in replay.assertions],
                         [(0xd3, 0xf7), (0x132, 0xd3)])

    def test_bar_fixture_tracks_npc_spawn_as_native_location_transition(self):
        fixture = pathlib.Path(__file__).parent / 'fixtures/gateway-bar-evening.walk'
        replay = compile_steps(parse_script(fixture.read_text(encoding='utf-8')))
        self.assertEqual(len(replay.actions), 24)
        self.assertEqual(replay.duration_ms, 102000)
        markers = {checkpoint.label: checkpoint for checkpoint in replay.checkpoints}
        self.assertEqual([(obj, loc) for obj, loc, _ in
                          markers['blue_hell_before_thom'].assertions],
                         [(0xd3, 0xf4), (0x100, 0)])
        self.assertEqual([(obj, loc) for obj, loc, _ in
                          markers['thom_arrives'].assertions],
                         [(0xd3, 0xf4), (0x100, 0x137)])

    def test_keys_menu_waits_and_review_barrier(self):
        script = parse_script('''
COMMAND open drawer
TYPE 3
KEY enter
WAIT_MS 7000
KEY 0x4800
MANUAL click on the portrait
COMMAND look
''')
        with self.assertRaisesRegex(ValueError, 'human review required'):
            compile_steps(script)
        limited = compile_steps(script, max_actions=3)
        self.assertEqual([s.operation for s in limited.actions], ['COMMAND', 'TYPE', 'KEY'])
        self.assertEqual(limited.keys[-2:], [(21000, ord('3')), (24000, 0x1c0d)])
        self.assertEqual(compile_steps(script, max_actions=4).keys[-1], (34000, 0x4800))

    def test_confirmed_pv_menu_sequence_matches_individual_raw_keys(self):
        shortcut = compile_steps(parse_script('COMMAND put card in slot\n'
                                              'MENU_KEYS 1, N,N,0,0\nCOMMAND look\n'))
        expanded = compile_steps(parse_script('COMMAND put card in slot\n'
                                              'TYPE 1\nTYPE N\nTYPE N\nTYPE 0\n'
                                              'TYPE 0\nCOMMAND look\n'))
        self.assertEqual(shortcut.keys, expanded.keys)
        self.assertEqual(shortcut.duration_ms, expanded.duration_ms)
        self.assertEqual(shortcut.keys[-5:], expanded.keys[-5:])

    def test_checkpoint_selects_replay_prefix_and_final_assertions_only(self):
        steps = parse_script('''
COMMAND take book
ASSERT_LOCATION 123 d3
LABEL got_book
COMMAND drop book
ASSERT_LOCATION 123 ee
LABEL dropped_book
''')
        first = compile_steps(steps, through='got_book')
        self.assertEqual(first.assertions, [(0x123, 0xd3, 3)])
        self.assertEqual(first.checkpoints[0].label, 'got_book')
        self.assertEqual(first.checkpoints[0].assertions, ((0x123, 0xd3, 3),))
        self.assertEqual(first.checkpoints[0].time_ms, 20999)
        self.assertEqual(len(first.actions), 1)
        full = compile_steps(steps)
        self.assertEqual(full.assertions, [(0x123, 0xee, 6)])
        self.assertEqual([(mark.label, mark.time_ms) for mark in full.checkpoints],
                         [('got_book', 20999), ('dropped_book', 23999)])
        with self.assertRaisesRegex(ValueError, 'unknown label'):
            compile_steps(steps, through='unknown')
        with self.assertRaisesRegex(ValueError, 'cannot be combined'):
            compile_steps(steps, through='got_book', max_actions=1)

    def test_converter_keeps_unknown_ui_action_explicit(self):
        source = '''Instructions:
}open drawer
} take book
*** Type: 4 ***
*** Type: 1,N,N,0,0 ***
*** Press: left image ***
{ some document styling
>inventory
'''
        translated = convert_text(source)
        self.assertEqual([s.operation for s in parse_script(translated)],
                         ['COMMAND', 'COMMAND', 'TYPE', 'MENU_KEYS', 'MANUAL', 'COMMAND'])
        self.assertIn('MENU_KEYS 1,N,N,0,0', translated)
        self.assertIn('MANUAL UI press: left image', translated)
        with self.assertRaisesRegex(ValueError, 'human review'):
            compile_steps(parse_script(translated))
        self.assertEqual(len(compile_steps(parse_script(translated), max_actions=3).actions), 3)
        self.assertIn('MANUAL Menu sequence:', convert_text('*** Type: password,123 ***'))

    def test_strict_input_bounds(self):
        for source in ('COMMAND\n', 'BAD abc', 'COMMAND héllo', 'COMMAND ' + 'a' * 65,
                       'KEY f13', 'WAIT_MS 0', 'WAIT_MS 600001', 'LABEL a\nLABEL a',
                       'ASSERT_LOCATION dead beef', 'ASSERT_LOCATION 0 1',
                       'ASSERT_LOCATION 1 garbage'):
            with self.subTest(source=source), self.assertRaises(ValueError):
                parse_script(source)
        for source in ('MENU_KEYS 1', 'MENU_KEYS 1,', 'MENU_KEYS 1,enter',
                       'MENU_KEYS 1,#', 'MENU_KEYS 1,ü'):
            with self.subTest(source=source), self.assertRaises(ValueError):
                parse_script(source)
        with self.assertRaisesRegex(ValueError, 'one guest hour'):
            compile_steps(parse_script('COMMAND look\nWAIT_MS 600000\n' * 6))
        dense = compile_steps(parse_script('COMMAND ' + 'a' * 64 + '\nCOMMAND look'),
                              spacing_ms=100)
        self.assertTrue(all(a[0] < b[0] for a, b in zip(dense.keys, dense.keys[1:])))

    def test_plan_only_cli_does_not_need_original_assets(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / 'opening.walk'
            path.write_text('COMMAND open drawer\n', encoding='utf-8')
            result = subprocess.run([sys.executable, str(TOOLS / 'run_walkthrough.py'),
                                     'run', str(path), '--plan-only'],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('Plan: 1 actions', result.stdout)


if __name__ == '__main__':
    unittest.main()
