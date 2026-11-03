# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Asset-independent tests of the reviewed walkthrough-to-native-input pipeline."""

import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

TOOLS = pathlib.Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))

from run_walkthrough import (check_locations, compile_steps, convert_text, extract_scribd_html,
                             import_native_save, native_save_description, parse_script)
from run_walkthrough_campaign import (find_save, load_campaign, verified_save)
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

    def test_complete_office_fixture_writes_native_save_after_both_exchanges(self):
        fixture = pathlib.Path(__file__).parent / 'fixtures/gateway-full-office-save.walk'
        replay = compile_steps(parse_script(fixture.read_text(encoding='utf-8')))
        self.assertEqual((len(replay.actions), len(replay.keys), replay.duration_ms),
                         (32, 327, 126000))
        markers = {checkpoint.label: checkpoint for checkpoint in replay.checkpoints}
        self.assertEqual([(obj, loc) for obj, loc, _ in
                          markers['receptionist_magazine'].assertions], [(0x13a, 0xd3)])
        self.assertEqual([(obj, loc) for obj, loc, _ in
                          markers['office_both_exchanges'].assertions],
                         [(0x123, 0), (0x130, 0xd3)])
        self.assertEqual([(step.operation, step.argument) for step in replay.actions[-3:]],
                         [('COMMAND', 'save'), ('TYPE', 'OFFICEFULL'), ('KEY', 'enter')])

    def test_dataman_restore_stage_uses_original_save_and_menu_sequences(self):
        fixture = pathlib.Path(__file__).parent / 'fixtures/gateway-dataman-restore-save.walk'
        replay = compile_steps(parse_script(fixture.read_text(encoding='utf-8')))
        self.assertEqual((len(replay.actions), len(replay.keys), replay.duration_ms),
                         (10, 105, 168000))
        self.assertEqual([c.label for c in replay.checkpoints],
                         ['office_restored', 'dataman_start', 'dataman_first_library',
                          'dataman_second_library', 'dataman_done'])
        self.assertEqual([(obj, loc) for obj, loc, _ in replay.checkpoints[0].assertions],
                         [(0x13a, 0xd3), (0x130, 0xd3)])
        self.assertEqual([s.operation for s in replay.actions[-5:]],
                         ['MENU_KEYS', 'MENU_KEYS', 'COMMAND', 'TYPE', 'KEY'])

    def test_native_vr_and_armory_stages_preserve_required_rewards(self):
        fixtures = [('gateway-vr-restore-save.walk', (38, 357, 132000),
                     'vr_pin_received', (0x2a9, 0xd3), 'VRFULL'),
                    ('gateway-armory-class-restore-save.walk', (30, 231, 108000),
                     'class_badge_awarded', (0x132, 0xd3), 'CLASSFULL')]
        for name, counts, checkpoint, item, save in fixtures:
            with self.subTest(name=name):
                fixture = pathlib.Path(__file__).parent / 'fixtures' / name
                replay = compile_steps(parse_script(fixture.read_text(encoding='utf-8')))
                self.assertEqual((len(replay.actions), len(replay.keys), replay.duration_ms),
                                 counts)
                mark = next(c for c in replay.checkpoints if c.label == checkpoint)
                self.assertIn(item, [(obj, loc) for obj, loc, _ in mark.assertions])
                self.assertEqual((replay.actions[-2].argument, replay.actions[-1].argument),
                                 (save, 'enter'))

    def test_full_item_bar_stage_reaches_casino_and_claims_medallion(self):
        fixture = pathlib.Path(__file__).parent / 'fixtures/gateway-bar-nubar-restore-save.walk'
        replay = compile_steps(parse_script(fixture.read_text(encoding='utf-8')))
        self.assertEqual((len(replay.actions), len(replay.keys), replay.duration_ms),
                         (49, 322, 195000))
        markers = {mark.label: mark for mark in replay.checkpoints}
        self.assertEqual([(obj, loc) for obj, loc, _ in
                          markers['class_restored'].assertions],
                         [(0x132, 0xd3), (0x14c, 0xd3), (0x13a, 0xd3),
                          (0x2a9, 0xd3)])
        self.assertEqual([(obj, loc) for obj, loc, _ in
                          markers['medallion_received'].assertions], [(0x12d, 0xd3)])

    def test_fork_stage_transfers_original_tuning_fork_into_inventory(self):
        fixture = pathlib.Path(__file__).parent / 'fixtures/gateway-fork-restore-save.walk'
        replay = compile_steps(parse_script(fixture.read_text(encoding='utf-8')))
        self.assertEqual((len(replay.actions), len(replay.keys), replay.duration_ms),
                         (16, 172, 66000))
        self.assertEqual([(obj, loc) for obj, loc, _ in
                          replay.checkpoints[-1].assertions], [(0x127, 0xd3)])

    def test_native_terri_morning_and_first_ship_mission_checkpoints(self):
        fixtures = [
            ('gateway-terri-restore-save.walk', 'napkin_received',
             (0x139, 0xd3), 'TERRIFULL'),
            ('gateway-morning-restore-save.walk', 'terri_restored',
             (0x139, 0xd3), 'HANGARFULL'),
            ('gateway-mission-one-restore-save.walk', 'first_mission_complete',
             (0x113, 0xd3), 'MISSIONFULL1'),
        ]
        for filename, label, item, save in fixtures:
            with self.subTest(filename=filename):
                fixture = pathlib.Path(__file__).parent / 'fixtures' / filename
                replay = compile_steps(parse_script(fixture.read_text(encoding='utf-8')))
                marks = {mark.label: mark for mark in replay.checkpoints}
                self.assertIn(item, [(obj, loc) for obj, loc, _ in marks[label].assertions])
                self.assertEqual(replay.actions[-2].argument, save)
                self.assertEqual(replay.actions[-1].argument, 'enter')
        mission = pathlib.Path(__file__).parent / 'fixtures/gateway-mission-one-restore-save.walk'
        replay = compile_steps(parse_script(mission.read_text(encoding='utf-8')))
        self.assertEqual(replay.mouse_clicks, [(33000, 160, 114)])
        self.assertEqual((len(replay.actions), len(replay.keys), replay.duration_ms),
                         (10, 54, 118000))

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

    def test_yes_no_reply_is_a_distinct_original_modal_prompt_action(self):
        planned = compile_steps(parse_script('COMMAND wait until 02:00\n'
                                             'ANSWER N\nCOMMAND sleep\n'))
        spelled = compile_steps(parse_script('COMMAND wait until 02:00\n'
                                             'COMMAND N\nCOMMAND sleep\n'))
        self.assertEqual(planned.keys, spelled.keys)
        source = 'Do you want to continue waiting? } N\n}sleep\n'
        self.assertEqual([s.operation for s in parse_script(convert_text(source))],
                         ['ANSWER', 'COMMAND'])
        for invalid in ('ANSWER maybe', 'ANSWER Y N', 'ANSWER 0'):
            with self.assertRaises(ValueError):
                parse_script(invalid)

    def test_native_mouse_clicks_are_scheduled_outside_bios_key_stream(self):
        replay = compile_steps(parse_script('COMMAND sit\nMOUSE_CLICK 144 120\n'
                                            'MOUSE_CLICK 150 140\nCOMMAND look\n'))
        self.assertEqual(replay.mouse_clicks, [(21000, 144, 120), (24000, 150, 140)])
        self.assertEqual(len(replay.actions), 4)
        self.assertEqual(replay.keys, compile_steps(parse_script(
            'COMMAND sit\nWAIT_MS 6000\nCOMMAND look\n')).keys)
        for invalid in ('MOUSE_CLICK -1 10', 'MOUSE_CLICK 640 20',
                        'MOUSE_CLICK 20 480', 'MOUSE_CLICK 1', 'MOUSE_CLICK x y'):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                parse_script(invalid)

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

    def test_checkpoint_accepts_verified_object_table_in_saved_es(self):
        with tempfile.TemporaryDirectory() as directory:
            prefix = pathlib.Path(directory) / 'snapshot'
            prefix.with_suffix('.regs').write_text('DS=2c24\nES=5b46\n', encoding='ascii')
            prefix.with_suffix('.bin').write_bytes(b'RAM')
            objects = [{'location': 0}] * 734
            objects[0x130 - 1] = {'location': 0xd3}
            visited = []

            def fake_inspect(memory, ds):
                visited.append(ds)
                return objects if ds == 0x5b46 else []

            with mock.patch('inspect_object_state.inspect_objects', side_effect=fake_inspect):
                check_locations(prefix, [(0x130, 0xd3, 5)])
                self.assertEqual(visited, [0x2c24, 0x5b46])
                with self.assertRaisesRegex(ValueError, 'no verified 734-object table'):
                    check_locations(prefix, [(0x130, 0xd3, 5)], ds=0x2c24)

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

    def test_extract_document_only_from_locally_obtained_guide_html(self):
        html = ('<header>}not a command</header><div data-e2e="bot-text">'
                '<h1>Gateway</h1><p>}open drawer<br/ >}take all<br>'
                '*** Type: 1,N,N,0,0 ***</p></div><footer>}ignore</footer>')
        source = extract_scribd_html(html)
        self.assertEqual([s.operation for s in parse_script(convert_text(source))],
                         ['COMMAND', 'COMMAND', 'MENU_KEYS'])
        self.assertNotIn('ignore', source)
        self.assertNotIn('not a command', source)
        with self.assertRaisesRegex(ValueError, 'no bot-text document'):
            extract_scribd_html('<main>}look</main>')

    def test_native_save_import_preserves_authentic_description_and_refuses_conflicts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source = root / 'GATE001.SAV'
            source.write_bytes(b'OFFICE100'.ljust(33, b' ') + bytes(range(64)))
            self.assertEqual(native_save_description(source), 'OFFICE100')
            self.assertEqual(import_native_save(source, root / 'other'), 'OFFICE100')
            self.assertEqual((root / 'other' / source.name).read_bytes(), source.read_bytes())
            self.assertEqual(import_native_save(source, root / 'other'), 'OFFICE100')
            (root / 'other' / source.name).write_bytes(b'OTHER'.ljust(33, b' ') + b'123')
            with self.assertRaisesRegex(ValueError, 'cannot overwrite'):
                import_native_save(source, root / 'other')
            with self.assertRaisesRegex(ValueError, 'source and destination'):
                import_native_save(source, root)
            source.write_bytes(b'\0' * 100)
            with self.assertRaisesRegex(ValueError, 'unrecognized'):
                native_save_description(source)
            bad_name = root / 'MALICIOUS.SAV'
            bad_name.write_bytes(b'A' * 100)
            with self.assertRaisesRegex(ValueError, 'GATE###'):
                native_save_description(bad_name)

    def test_campaign_validates_native_save_handoffs_and_plans_stages(self):
        campaign = pathlib.Path(__file__).parent / 'fixtures/gateway-campaign.json'
        stages = load_campaign(campaign)
        self.assertEqual([stage.name for stage in stages],
                         ['office', 'dataman', 'vr', 'armory_class', 'bar_nubar', 'fork',
                          'terri', 'morning', 'mission_one'])
        self.assertEqual([stage.description for stage in stages],
                         ['OFFICEFULL', 'DATAMAN100', 'VRFULL', 'CLASSFULL', 'FULLCASINO',
                          'FORKFULL', 'TERRIFULL', 'HANGARFULL', 'MISSIONFULL1'])
        self.assertEqual(sum(stage.actions for stage in stages), 225)
        result = subprocess.run([sys.executable,
                                 str(TOOLS / 'run_walkthrough_campaign.py'),
                                 str(campaign), '--plan-only'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('bar_nubar: 49 actions', result.stdout)

    def test_campaign_rejects_unreviewed_restore_and_modified_previous_save(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / 'first.walk').write_text('COMMAND look\nCOMMAND save\n'
                                             'TYPE STAGEONE\nKEY enter\n', encoding='ascii')
            (root / 'second.walk').write_text('COMMAND look\nCOMMAND save\n'
                                              'TYPE STAGETWO\nKEY enter\n', encoding='ascii')
            manifest = root / 'campaign.json'
            manifest.write_text(json.dumps({'schema_version': 1, 'stages': [
                {'name': 'first', 'script': 'first.walk', 'save': 'STAGEONE'},
                {'name': 'second', 'script': 'second.walk', 'save': 'STAGETWO'}]}))
            with self.assertRaisesRegex(ValueError, 'restoring previous save'):
                load_campaign(manifest)
            (root / 'second.walk').write_text('COMMAND restore\nTYPE STAGEONE\n'
                                              'KEY enter\nCOMMAND save\n'
                                              'TYPE STAGETWO\nKEY enter\n', encoding='ascii')
            first, second = load_campaign(manifest)
            self.assertEqual((first.actions, second.actions), (4, 6))
            state = root / 'output' / 'first' / 'state'
            state.mkdir(parents=True)
            saved = state / 'GATE001.SAV'
            saved.write_bytes(b'STAGEONE'.ljust(33, b' ') + b'A' * 60)
            self.assertEqual(find_save(state, 'STAGEONE'), saved)
            meta = {'name': first.name, 'script_sha256': first.script_sha256,
                    'save_description': first.description,
                    'save_sha256': hashlib.sha256(saved.read_bytes()).hexdigest()}
            (state.parent / 'stage-meta.json').write_text(json.dumps(meta), encoding='ascii')
            self.assertEqual(verified_save(root / 'output', first), saved)
            saved.write_bytes(saved.read_bytes() + b'B')
            with self.assertRaisesRegex(ValueError, 'was modified'):
                verified_save(root / 'output', first)

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

    def test_verify_existing_refuses_stale_input_without_rerunning_original_game(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source = root / 'route.walk'
            source.write_text('COMMAND look\n', encoding='ascii')
            (root / 'replay.keys').write_text('0:1234\n', encoding='ascii')
            result = subprocess.run([sys.executable, str(TOOLS / 'run_walkthrough.py'),
                                     'run', str(source), '--verify-existing',
                                     '--output', str(root)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('recorded replay.keys differs', result.stderr)
            self.assertNotIn('original GATE.EXE', result.stderr)


if __name__ == '__main__':
    unittest.main()
