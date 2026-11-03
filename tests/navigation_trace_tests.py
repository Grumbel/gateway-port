# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Player location-change reports from synthetic native probe traces."""
from pathlib import Path
import json
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from summarize_navigation_trace import (location_events, map_location_setter_return,
                                        move_helper_calls, room_names, summarize,
                                        summarize_direction_calls, direction_dispatch_calls,
                                        summarize_callback_returns,
                                        callback_return_entries, summarize_move_callers,
                                        summarize_room_heads, puzzle_input_slots,
                                        recent_input)

TOOL = ROOT / 'tools' / 'summarize_navigation_trace.py'
WATCH = 'object-watch ms=11100 object=d3 type=7 at=1e4c:1234 size=20 child_count=3'
WRITE_OUT = ('object-write ms=18130 object=d3 pc=43abc cs=4319 ip=abc '
             'offset=c size=2 before=ee00 value=f100')
WRITE_BACK = ('object-write ms=21130 object=d3 pc=43abd cs=4319 ip=abd '
              'offset=c size=2 before=f100 value=ee00')
SET_OUT = ('object-location-set ms=18130 object=d3 location=f1 '
           'write=1e4c:1240:f1,0, changed=1 return=4319:1122')
SET_BACK = ('object-location-set ms=21130 object=d3 location=ee '
            'write=1e4c:1240:ee,0, changed=1 return=4319:1123')
COMMANDS = ('east', 'west', 'east', 'west', 'look', 'inventory', 'look')
SECTION_4 = dict(id=4, runtime_segment=0x4319, image_file_offset=0xb1e0,
                 file_bytes=0x1ac0, filename='GATEMAIN.OVL')


def overlay_fixture():
    """Include a false same-segment match in section 3, as with real RTLink."""
    manifest = {'sections': [
        dict(id=3, runtime_segment=0x4319, image_file_offset=0x9300,
             file_bytes=0x1c80, filename='GATEMAIN.OVL'), SECTION_4]}
    overlay = bytearray(0xd000)
    overlay[0xc8c2:0xc8c7] = bytes.fromhex('9a4b065701')
    return manifest, overlay


def head_call(ms, object_id, child, insert):
    return (f'entry ms={ms} pc=11e6c cs=1157 '
            f'stack={"171d" if insert else "169e"},4319,0,0 '
            f'child_set_object={object_id:x} child_set_slot=0 '
            f'child_set_value={child:x}')


def move_entry(ms, object_id, destination, slot=0):
    return (f'entry ms={ms} pc=447e0 cs=4319 ss=5b46 sp=db88 bp=db9a '
            f'stack=701,2c24,{object_id:x},{destination:x},{slot:x},0,ee,343b '
            'frames=db9a:dbb6:65a:148e,dbb6:dbc2:c28:148e,'
            'dbc2:dc28:4da:1000,')


def direction_entry(ms, code):
    return (f'entry ms={ms} pc=14ed6 cs=148e ds=5b46 '
            f'ss=5b46 sp=dbba bp=dbc2 stack=c28,148e,{code:x},0,40e '
            'frames=dbc2:dc28:4da:1000,')


def callback_entry(ms, target):
    return f'entry ms={ms} pc=1507c cs=148e ax={target:x} stack=0,0,0'


class NavigationTraceTests(unittest.TestCase):
    def test_extended_puzzle_input_labels_keep_the_original_seven_slots(self):
        commands = ('wait',) * 41 + ('out',)
        slots = puzzle_input_slots(commands)
        self.assertEqual((slots[0], slots[-1]), (18000, 141000))
        self.assertEqual(recent_input(141175, commands, slots), 'out')
        self.assertEqual(recent_input(17700, commands, slots), 'startup')
        self.assertEqual(recent_input(21200, COMMANDS), 'west')
        with self.assertRaisesRegex(ValueError, 'must not be empty'):
            puzzle_input_slots(())

        line = callback_entry(141175, 0)
        report = summarize_callback_returns([], [line], commands, slots=slots)
        self.assertIn('141175 out                0x0000', report)
        direction = summarize_direction_calls([], [direction_entry(141175, 9)],
                                              commands, slots=slots)
        self.assertIn('141175 out                0x09', direction)
        event = [dict(ms=141175, old=0x29a, new=0x0f6,
                      evidence='verified service', caller='4319:1122', pc='1')]
        self.assertIn('141175 out', summarize(event, commands=commands, slots=slots))

    def test_verified_move_without_player_watch_reports_destination_not_source(self):
        # A pin record watch still records original player-location setter
        # results, but cannot recover the old player location word.
        move = [dict(ms=132268, old=None, new=0x0f6,
                     evidence='verified service', caller='4319:16e7', pc='?')]
        scan = summarize_callback_returns(move, [callback_entry(132268, 0x0f6)])
        self.assertIn('to 0x0f6 @132268 (unwatched source)', scan)
        move_report = summarize_direction_calls(move, [direction_entry(132176, 3)])
        self.assertIn('? -> 0x0f6 @132268 (unwatched source)', move_report)
        self.assertNotIn('0x000 ->', move_report)

    def test_changed_service_and_matching_prewrite(self):
        lines = [WATCH, SET_OUT, WRITE_OUT, SET_BACK, WRITE_BACK,
                 'object-location-set ms=25000 object=123 location=d3 changed=1',
                 'object-location-set ms=26000 object=d3 location=ee '
                 'changed=0 return=1157:00ab',
                 'object-write ms=26200 object=d3 pc=1 offset=10 size=2 '
                 'before=0000 value=ffff']
        events = location_events(lines)
        self.assertEqual(len(events), 2)
        self.assertEqual([(event['old'], event['new']) for event in events],
                         [(0xee, 0xf1), (0xf1, 0xee)])
        self.assertEqual(events[0]['evidence'], 'verified service')
        self.assertEqual(events[0]['caller'], '4319:1122')
        self.assertEqual(events[0]['pc'], '43abc')
        world = dict(roots=[dict(id=0xee, native_type=1, name='Your Quarters',
                                 child_slots=[]),
                            dict(id=0xf1, native_type=1, name='Heecheetown',
                                 child_slots=[])])
        output = summarize(events, room_names(world), COMMANDS)
        self.assertIn('east', output)
        self.assertIn('west', output)
        self.assertIn('0x0ee Your Quarters -> 0x0f1 Heecheetown', output)
        self.assertIn('0x0f1 Heecheetown -> 0x0ee Your Quarters', output)
        self.assertIn('timed association', output)

    def test_prewrite_is_not_mistaken_for_verified_navigation(self):
        events = location_events([WATCH, WRITE_OUT])
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0]['evidence'], 'pre-write attempt')
        self.assertIn('may not complete', summarize(events))
        self.assertIn('no nonredundant', summarize([]))
        with self.assertRaisesRegex(ValueError, 'without verified object-watch'):
            location_events([WRITE_OUT])

    def test_inconsistent_native_annotations_are_rejected(self):
        unchanged = SET_OUT.replace('changed=1', 'changed=0')
        with self.assertRaisesRegex(ValueError, 'disagrees'):
            location_events([WATCH, unchanged, WRITE_OUT])
        redundant = WRITE_OUT.replace('before=ee00', 'before=f100')
        with self.assertRaisesRegex(ValueError, 'disagrees'):
            location_events([WATCH, SET_OUT, redundant])
        with self.assertRaisesRegex(ValueError, 'not native type 7'):
            location_events([WATCH.replace('type=7', 'type=2')])
        with self.assertRaisesRegex(ValueError, 'invalid native player trace'):
            location_events([WATCH, WRITE_OUT.replace('before=ee00', 'before=z')])

    def test_scripted_command_window_and_cli(self):
        world = {'roots': [dict(id=0xee, native_type=1, name='Your Quarters',
                                child_slots=[]),
                           dict(id=0xf1, native_type=1, name='Heecheetown',
                                child_slots=[])]}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            trace = root / 'native.trace'
            trace.write_text('\n'.join([WATCH, SET_OUT, WRITE_OUT]))
            names = root / 'world.json'
            names.write_text(json.dumps(world))
            command = [sys.executable, str(TOOL), str(trace), '--world', str(names),
                       '--commands', ','.join(COMMANDS)]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('east', result.stdout)
            self.assertIn('Heecheetown', result.stdout)
            bad = subprocess.run(command[:-1] + ['east'], capture_output=True, text=True)
            self.assertEqual(bad.returncode, 2)
            self.assertIn('expected 7', bad.stderr)

    def test_direct_location_setter_far_call_requires_original_bytes(self):
        manifest, overlay = overlay_fixture()
        self.assertEqual(map_location_setter_return('4319:16e7', manifest, overlay),
                         (4, 0xc8c2))
        self.assertIsNone(map_location_setter_return('4319:16e8', manifest, overlay))
        self.assertIsNone(map_location_setter_return('4319:1122', manifest, overlay))
        events = location_events([WATCH, SET_OUT, WRITE_OUT])
        unmatched = summarize(events, manifest=manifest, overlay=overlay)
        self.assertIn('original=unresolved (no matching direct call)', unmatched)
        expected = SET_OUT.replace('return=4319:1122', 'return=4319:16e7')
        report = summarize(location_events([WATCH, expected, WRITE_OUT]),
                           manifest=manifest, overlay=overlay)
        self.assertIn('original=section=4 GATEMAIN.OVL:0xc8c2', report)
        overlay[0xc8c2] = 0x90
        self.assertIsNone(map_location_setter_return('4319:16e7', manifest, overlay))
        with self.assertRaisesRegex(ValueError, 'supplied together'):
            summarize(events, manifest=manifest)
        overlay[0xc8c2] = 0x9a
        manifest['sections'].append(dict(SECTION_4, id=5))
        with self.assertRaisesRegex(ValueError, 'ambiguous'):
            map_location_setter_return('4319:16e7', manifest, overlay)

    def test_cli_overlay_verification_and_pair_requirement(self):
        manifest, overlay = overlay_fixture()
        overlay[0xc8f8:0xc8fd] = bytes.fromhex('9afc085701')
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            trace = root / 'native.trace'
            trace.write_text('\n'.join([
                WATCH, SET_OUT.replace('return=4319:1122', 'return=4319:16e7'),
                WRITE_OUT, head_call(18130, 0xf1, 0xd3, True)]))
            manifest_path = root / 'manifest.json'
            manifest_path.write_text(json.dumps(manifest))
            overlay_path = root / 'GATEMAIN.OVL'
            overlay_path.write_bytes(overlay)
            command = [sys.executable, str(TOOL), str(trace),
                       '--overlay-map', str(manifest_path), '--overlay-file',
                       str(overlay_path)]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('section=4 GATEMAIN.OVL:0xc8c2', result.stdout)
            with_heads = subprocess.run(command + ['--child-heads'],
                                        capture_output=True, text=True)
            self.assertEqual(with_heads.returncode, 0, with_heads.stderr)
            self.assertIn('0x0f1: 0x0d3 @18130', with_heads.stdout)
            missing = subprocess.run(command[:-2], capture_output=True, text=True)
            self.assertEqual(missing.returncode, 2)
            self.assertIn('--overlay-map and --overlay-file', missing.stderr)

    def test_child_heads_follow_initial_middle_then_head_removals(self):
        manifest, overlay = overlay_fixture()
        for address in (0xc879, 0xc8f8):
            overlay[address:address + 5] = bytes.fromhex('9afc085701')
        commands = [
            (18266, 0xee, 0xf1), (21177, 0xf1, 0xee),
            (25261, 0xee, 0xf1), (28177, 0xf1, 0xee)]
        events = [dict(ms=ms, old=old, new=new, evidence='verified service')
                  for ms, old, new in commands]
        lines = [
            head_call(18266, 0xf1, 0xd3, True),
            head_call(21177, 0xf1, 0x194, False),
            head_call(21177, 0xee, 0xd3, True),
            head_call(21177, 0x161, 0, False),  # unrelated same-time setter
            head_call(25260, 0xee, 0x175, False),
            head_call(25261, 0xf1, 0xd3, True),
            head_call(28177, 0xf1, 0x194, False),
            head_call(28177, 0xee, 0xd3, True)]
        report = summarize_room_heads(events, lines, manifest, overlay)
        self.assertIn('18266  0x0ee: not observed', report)
        self.assertIn('0x0f1: 0x0d3 @18266', report)
        self.assertIn('0x0f1: 0x194 @21177', report)
        self.assertIn('0x0ee: 0x175 @25260', report)
        self.assertIn('0x0ee: 0x0d3 @28177', report)
        self.assertIn('pre-write attempts, not verified', report)
        self.assertNotIn('0x161', report)
        self.assertIn('rerun with --trace-object-writes',
                      summarize_room_heads(events, [], manifest, overlay))
        repeated = lines + [head_call(25261, 0xf1, 0xd3, True)]
        self.assertIn('ambiguous (2 setters)',
                      summarize_room_heads(events, repeated, manifest, overlay))

    def test_cli_child_heads_requires_verified_original_overlay(self):
        with tempfile.TemporaryDirectory() as directory:
            trace = Path(directory) / 'native.trace'
            trace.write_text('\n'.join([WATCH, SET_OUT, WRITE_OUT]))
            result = subprocess.run([sys.executable, str(TOOL), str(trace),
                                     '--child-heads'], capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertIn('--child-heads requires --overlay-map', result.stderr)

    def test_helper_entry_caller_frames_and_verified_move_correlations(self):
        entries = [move_entry(5408, 0x16d, 0x161),
                   move_entry(18266, 0xd3, 0xf1),
                   move_entry(21177, 0xd3, 0xee),
                   move_entry(25260, 0xd3, 0xf1),
                   move_entry(28177, 0xd3, 0xee),
                   move_entry(28177, 0xd3, 0xee, slot=1),
                   move_entry(29000, 0xd3, 0xee).replace('stack=701,2c24',
                                                            'stack=700,2c24')]
        calls = move_helper_calls(entries)
        self.assertEqual(len(calls), 5)  # two at 28177, one wrong slot
        self.assertEqual(calls[0]['frames'],
                         ['148e:065a', '148e:0c28', '1000:04da'])
        events = [dict(ms=ms, old=old, new=new, evidence='verified service')
                  for ms, old, new in ((18266, 0xee, 0xf1),
                                       (21177, 0xf1, 0xee),
                                       (25261, 0xee, 0xf1),
                                       (28177, 0xf1, 0xee))]
        report = summarize_move_callers(events, entries)
        self.assertIn('18266  0x0d3 -> 0x0f1 / 0', report)
        self.assertIn('25261  0x0d3 -> 0x0f1 / 0', report)
        self.assertIn('148e:065a <- 148e:0c28 <- 1000:04da', report)
        self.assertIn('RTLink manager plumbing', report)
        self.assertNotIn('0x161', report)
        self.assertIn('no corroborated move entries',
                      summarize_move_callers(events, []))
        duplicate = entries + [move_entry(21177, 0xd3, 0xee)]
        self.assertIn('ambiguous: 2 matching entries',
                      summarize_move_callers(events, duplicate))
        malformed = move_entry(21177, 0xd3, 0xee).replace(
            'db9a:dbb6:65a:148e', 'bad:frame')
        with self.assertRaisesRegex(ValueError, 'invalid move-helper entry'):
            move_helper_calls([malformed])

    def test_cli_move_callers_without_original_game_assets(self):
        with tempfile.TemporaryDirectory() as directory:
            trace = Path(directory) / 'native.trace'
            trace.write_text('\n'.join([WATCH,
                                        SET_OUT.replace('return=4319:1122',
                                                        'return=4319:16e7'),
                                        WRITE_OUT, move_entry(18130, 0xd3, 0xf1)]))
            result = subprocess.run([sys.executable, str(TOOL), str(trace),
                                     '--move-callers'], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('148e:065a', result.stdout)

    def test_resident_exit_direction_byte_is_verified_by_far_return_frame(self):
        lines = [direction_entry(18250, 0x42),
                 direction_entry(21160, 0x57),
                 direction_entry(28000, 0x99).replace('cs=148e', 'cs=148f'),
                 direction_entry(28300, 0x7f).replace('stack=c28', 'stack=abc'),
                 'entry ms=30000 pc=14f3a cs=148e stack=c28,148e,57']
        self.assertEqual(direction_dispatch_calls(lines), [
            dict(ms=18250, direction=0x42, return_address='148e:0c28'),
            dict(ms=21160, direction=0x57, return_address='148e:0c28')])
        events = [dict(ms=18266, old=0xee, new=0xf1,
                       evidence='verified service'),
                  dict(ms=21177, old=0xf1, new=0xee,
                       evidence='verified service'),
                  dict(ms=21178, old=0xf1, new=0xee,
                       evidence='pre-write attempt')]
        output = summarize_direction_calls(events, lines, COMMANDS,
                                           {0xee: 'Quarters', 0xf1: 'Town'})
        self.assertIn('18250 east               0x42', output)
        self.assertIn('Quarters -> 0x0f1 Town @18266', output)
        self.assertIn('21160 west               0x57', output)
        self.assertNotIn('21178', output)
        self.assertIn('scheduled time slots', output)
        with self.assertRaisesRegex(ValueError, 'not a byte'):
            direction_dispatch_calls([direction_entry(18100, 0x100)])
        self.assertIn('no verified 148e:05f6 call frames',
                      summarize_direction_calls(events, []))

    def test_direction_entries_do_not_attach_distant_or_ambiguous_moves(self):
        calls = [direction_entry(18300, 0x01),
                 direction_entry(21000, 0x02),
                 direction_entry(26000, 0x03),
                 direction_entry(26100, 0x04)]
        events = [dict(ms=18400, old=1, new=2, evidence='verified service'),
                  dict(ms=18500, old=2, new=3, evidence='verified service'),
                  dict(ms=21600, old=4, new=5, evidence='verified service'),
                  dict(ms=26150, old=6, new=7, evidence='verified service')]
        report = summarize_direction_calls(events, calls)
        self.assertIn('ambiguous: 2 verified moves', report)
        self.assertIn('none within 500 guest ms', report)
        self.assertIn('0x03             (none', report)
        self.assertIn('0x04             0x006 -> 0x007 @26150', report)
        boundary = summarize_direction_calls(
            [dict(ms=26050, old=1, new=2, evidence='verified service')],
            [direction_entry(26000, 0x03), direction_entry(26050, 0x04)])
        self.assertIn('0x03             (none', boundary)
        self.assertIn('0x04             0x001 -> 0x002 @26050', boundary)

    def test_scripted_exit_kind4_callback_returns_are_separate_from_moves(self):
        lines = [callback_entry(18200, 0xf1),
                 callback_entry(21200, 0),
                 callback_entry(25190, 0x124),
                 callback_entry(28200, 0xee).replace('cs=148e', 'cs=148f'),
                 callback_entry(31200, 0xf1).replace('pc=1507c', 'pc=15070')]
        self.assertEqual(callback_return_entries(lines), [
            dict(ms=18200, target=0xf1), dict(ms=21200, target=0),
            dict(ms=25190, target=0x124)])
        events = [dict(ms=18266, old=0xee, new=0xf1,
                       evidence='verified service'),
                  dict(ms=25195, old=0xee, new=0x124,
                       evidence='pre-write attempt')]
        report = summarize_callback_returns(events, lines, COMMANDS, {0xf1: 'Town'})
        self.assertIn('18200 east               0x00f1', report)
        self.assertIn('to 0x0f1 Town @18266', report)
        self.assertIn('21200 west               0x0000', report)
        self.assertIn('no matching move within 500 guest ms', report)
        self.assertNotIn('25195', report)
        self.assertIn('not prove parser acceptance', report)
        self.assertIn('no kind-4 callback returns',
                      summarize_callback_returns(events, []))
        with self.assertRaisesRegex(ValueError, 'invalid kind-4 callback'):
            callback_return_entries([callback_entry(18100, 0).replace('ax=0', 'ax=oops')])

    def test_kind4_gate_id_trampoline_and_two_return_values_correlate(self):
        calls = [
            'entry ms=28247 pc=15077 cs=148e room_callback_stage=scanner-call '
            'room_callback_id=14 room_callback_event=f',
            'entry ms=28247 pc=14759 cs=1474 room_callback_stage=gate-call '
            'room_callback_id=14 room_callback_event=f '
            'room_callback_target=2c24:14d9 room_callback_section=30',
            'entry ms=28247 pc=1475e cs=1474 room_callback_stage=gate-return '
            'room_callback_result=0',
            callback_entry(28247, 0) +
            ' room_callback_stage=scanner-return room_callback_result=0']
        self.assertEqual(callback_return_entries(calls), [
            dict(ms=28247, call_ms=28247, target=0, callback_id=20, event=15,
                 target_pointer='2c24:14d9', section=48)])
        report = summarize_callback_returns([], calls, COMMANDS)
        self.assertIn('callback 0x14, event=0x0f, pointer=2c24:14d9, section=48', report)
        with self.assertRaisesRegex(ValueError, 'disagree'):
            callback_return_entries([*calls[:3], callback_entry(28247, 0xf6)])
        self.assertEqual(callback_return_entries([
            calls[1].replace('room_callback_event=f', 'room_callback_event=1'),
            calls[2].replace('room_callback_result=0', 'room_callback_result=f6'),
            callback_entry(28247, 0)]), [dict(ms=28247, target=0)])
        with self.assertRaisesRegex(ValueError, 'arguments disagree'):
            callback_return_entries([calls[0].replace('room_callback_id=14',
                                                      'room_callback_id=13'), *calls[1:]])
        # Guest time advances while the callback prints text; different
        # timestamps between its call and return still identify one call.
        late = [calls[0].replace('28247', '28176'),
                calls[1].replace('28247', '28176'), *calls[2:]]
        correlated = callback_return_entries(late)[0]
        self.assertEqual(correlated['callback_id'], 0x14)
        self.assertEqual((correlated['call_ms'], correlated['ms']), (28176, 28247))


if __name__ == '__main__':
    unittest.main()
