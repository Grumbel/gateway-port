# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Game-data-independent structural checks for original Gateway save files."""
import contextlib
import hashlib
import io
import json
import pathlib
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))

from inspect_gateway_save import (DESCRIPTION_BYTES, EVENT_BYTES, EVENTS_OFFSET,
                                  GLOBAL_STATE_BYTES, OBJECT_BYTES, OBJECT_KINDS,
                                  OBJECT_RECORD_SIZES, compare_saved_state,
                                  parse_gateway_save)
from inspect_object_state import HANDLER_FIELDS, LOCATION_FIELDS
from locate_save_ascii import occurrences, saved_ascii_field, trace_field
from run_golden import digest_snapshot


def fake_gateway_save(description='EXAMPLE', room=0x0a, previous=0x09,
                      turns=0x0163, events=((0x25, 230), (0x2a, 290))):
    """Create a format-valid stand-in; no original DOS data is embedded."""
    data = bytearray(EVENTS_OFFSET + EVENT_BYTES * len(events))
    data[:33] = description.encode('ascii').ljust(33, b' ')
    data[33] = 0
    offset = DESCRIPTION_BYTES
    for object_id, digit in enumerate(OBJECT_KINDS, 1):
        kind = int(digit)
        size = OBJECT_RECORD_SIZES[kind]
        struct.pack_into('<H', data, offset, object_id)  # arbitrary stand-in token
        if kind in LOCATION_FIELDS:
            struct.pack_into('<H', data, offset + LOCATION_FIELDS[kind],
                             room if object_id == 211 else 0)
        if object_id == 211:
            struct.pack_into('<H', data, offset + HANDLER_FIELDS[kind - 1], 696)
        offset += size
    assert offset == DESCRIPTION_BYTES + OBJECT_BYTES
    struct.pack_into('<HH', data, offset, room, previous)
    data[offset + 4:offset + 6] = turns.to_bytes(2, 'big')
    assert EVENTS_OFFSET - offset - 2 == GLOBAL_STATE_BYTES
    struct.pack_into('<H', data, EVENTS_OFFSET - 2, len(events))
    for index, (event_id, remaining) in enumerate(events):
        struct.pack_into('<HH', data, EVENTS_OFFSET + index * EVENT_BYTES,
                         event_id, remaining)
    return data


class GatewaySaveTests(unittest.TestCase):
    def test_locate_differing_ascii_without_guessing_memory_copy(self):
        first = fake_gateway_save()
        second = fake_gateway_save()
        base = DESCRIPTION_BYTES + OBJECT_BYTES + 0x282
        first[base:base + 7] = b'10-4321'
        second[base:base + 7] = b'22-9876'
        saved_a, saved_b = [parse_gateway_save(data) for data in (first, second)]
        self.assertEqual(saved_ascii_field(saved_a, 0x282, 7), b'10-4321')
        self.assertEqual(saved_ascii_field(saved_b, 0x282, 7), b'22-9876')
        for invalid in (-1, 1070):
            with self.assertRaisesRegex(ValueError, 'exceeds'):
                saved_ascii_field(saved_a, invalid, 7)
        with self.assertRaisesRegex(ValueError, 'printable'):
            saved_ascii_field(saved_a, 0x282 - 1, 7)
        memory = bytearray(1024 * 1024)
        memory[0x12345:0x1234c] = b'10-4321'
        memory[0x9fffc:0xa0003] = b'10-4321'
        self.assertEqual(occurrences(memory, b'10-4321'), [0x12345])
        with self.assertRaisesRegex(ValueError, 'expected'):
            occurrences(memory[:-1], b'10-4321')

    def test_trace_ascii_field_checks_original_saves_and_golden_snapshots(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            manifests = []
            for name, number in (('a', b'10-4321'), ('b', b'22-9876')):
                stage_root = root / name / 'terri'
                (stage_root / 'state').mkdir(parents=True)
                data = fake_gateway_save()
                data[DESCRIPTION_BYTES + OBJECT_BYTES + 0x282:
                     DESCRIPTION_BYTES + OBJECT_BYTES + 0x289] = number
                (stage_root / 'state/GATE007.SAV').write_bytes(data)
                (stage_root / 'stage-meta.json').write_text(json.dumps({
                    'name': 'terri', 'script_sha256': 'script',
                    'save_description': 'EXAMPLE',
                    'save_sha256': hashlib.sha256(data).hexdigest(),
                }))
                checkpoints = []
                for label in ('before', 'final'):
                    prefix = stage_root / ('native' if label == 'final' else
                                           'checkpoint-before')
                    memory = bytearray(1024 * 1024)
                    if label == 'final':
                        memory[0x12345:0x1234c] = number
                    prefix.with_suffix('.bin').write_bytes(memory)
                    prefix.with_suffix('.regs').write_text('SS=8000\nSP=fffe\nDS=1000\n')
                    point = dict(label=label, time_ms=50 if label == 'final' else 10,
                                 action_index=1, command_index=1)
                    checkpoints.append(digest_snapshot(prefix, point))
                manifests.append(dict(campaign_sha256='campaign', stages=[dict(
                    name='terri', script_sha256='script', checkpoints=checkpoints,
                    save_sha256=hashlib.sha256(data).hexdigest())]))
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                trace_field(manifests[0], manifests[1], root / 'a', root / 'b',
                            'terri', 0x282, 7)
            self.assertIn('run-a=\'10-4321\' run-b=\'22-9876\'', output.getvalue())
            self.assertIn('before (command 1, 10ms):', output.getvalue())
            self.assertIn('same-address[0]: -', output.getvalue())
            self.assertIn('same-address[1]: 0x12345', output.getvalue())
            old_manifest = [dict(manifests[0], stages=[
                {key: value for key, value in manifests[0]['stages'][0].items()
                 if key != 'save_sha256'}]), manifests[1]]
            with contextlib.redirect_stdout(output := io.StringIO()):
                trace_field(old_manifest[0], old_manifest[1],
                            root / 'a', root / 'b', 'terri', 0x282, 7)
            self.assertIn('authenticated against stage metadata only', output.getvalue())
            self.assertIn('same-address[1]: 0x12345', output.getvalue())
            corrupted_manifest = [manifests[0], dict(manifests[1], stages=[
                dict(manifests[1]['stages'][0], save_sha256='0' * 64)])]
            with contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaisesRegex(ValueError, 'differs from golden manifest'):
                    trace_field(corrupted_manifest[0], corrupted_manifest[1],
                                root / 'a', root / 'b', 'terri', 0x282, 7)
            (root / 'b/terri/native.bin').write_bytes(b'broken')
            with contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaisesRegex(ValueError, 'expected'):
                    trace_field(manifests[0], manifests[1], root / 'a', root / 'b',
                                'terri', 0x282, 7)

    def test_save_comparison_isolates_objects_globals_and_timer_drift(self):
        events = ((0x25, 230), (0x2a, 290))
        previous = parse_gateway_save(fake_gateway_save(turns=0x0163, events=events))
        next_turn = parse_gateway_save(fake_gateway_save(
            turns=0x0164, events=((0x25, 229), (0x2a, 289))))
        diff = compare_saved_state(previous, next_turn)
        self.assertTrue(diff['turn_shift_consistent'])
        self.assertEqual(diff['object_records_changed'], 0)
        self.assertEqual([entry['offset'] for entry in diff['global_byte_changes']], [5])
        self.assertEqual(len(diff['event_changes']), 2)
        self.assertEqual(diff['turn_counter'], [0x163, 0x164])

        changed = fake_gateway_save(turns=0x0164, events=((0x25, 229), (0x2a, 289)))
        changed[DESCRIPTION_BYTES + OBJECT_BYTES + 0x30] = 1
        changed[DESCRIPTION_BYTES + 12] ^= 1  # one ordinary object record byte
        new_diff = compare_saved_state(previous, parse_gateway_save(changed))
        self.assertFalse(new_diff['turn_shift_consistent'])
        self.assertEqual(new_diff['object_records_changed'], 1)
        self.assertEqual(new_diff['object_ids'], [1])
        self.assertEqual([entry['offset'] for entry in new_diff['global_byte_changes']],
                         [5, 0x30])

    def test_golden_save_diff_formatter_reports_decoded_and_unknown_fields(self):
        from run_golden import describe_save_payload_difference

        with tempfile.TemporaryDirectory() as directory:
            a, b = [pathlib.Path(directory) / name for name in ('a.sav', 'b.sav')]
            a.write_bytes(fake_gateway_save(turns=0x0163, events=((0x25, 230),)))
            changed = fake_gateway_save(turns=0x0164, events=((0x25, 229),))
            changed[DESCRIPTION_BYTES + OBJECT_BYTES + 0x20] = 77
            b.write_bytes(changed)
            details = '\n'.join(describe_save_payload_difference(a, b))
            self.assertIn('object records changed 0/734', details)
            self.assertIn('global bytes changed 2/1071', details)
            self.assertIn('+0x005', details)
            self.assertIn('+0x020', details)
            self.assertIn('provisional turn counter a/b=355/356', details)
            self.assertIn('event records [id, remaining] a/b', details)
            self.assertNotIn('consistent with a turn-count offset', details)

    def test_original_object_layout_has_exact_record_sizes(self):
        self.assertEqual(len(OBJECT_KINDS), 734)
        self.assertEqual({kind: OBJECT_KINDS.count(str(kind)) for kind in range(1, 9)},
                         {1: 110, 2: 241, 3: 24, 4: 8,
                          5: 63, 6: 15, 7: 46, 8: 227})
        self.assertEqual(sum(OBJECT_RECORD_SIZES[int(x)] for x in OBJECT_KINDS), 20650)
        self.assertEqual(EVENTS_OFFSET, 21757)

    def test_parse_all_objects_and_standalone_timer_queue(self):
        for events in ((), ((0x61, 0),),
                       ((0x21, 145), (0x17, 446), (0x18, 566),
                        (0x25, 686), (0x2a, 746))):
            with self.subTest(events=events):
                raw = fake_gateway_save(events=events)
                save = parse_gateway_save(raw)
                self.assertEqual(save.file_bytes, 21757 + 4 * len(events))
                self.assertEqual(save.description, 'EXAMPLE')
                self.assertEqual((save.current_room, save.previous_room,
                                  save.turn_counter), (0x0a, 0x09, 0x0163))
                self.assertEqual([(x.event_id, x.remaining_turns) for x in save.events],
                                 list(events))
                self.assertEqual(len(save.objects), 734)
                self.assertEqual(save.objects[210].object_id, 211)
                self.assertEqual(save.objects[210].location, 0x0a)
                self.assertEqual(save.objects[210].handler, 696)
                self.assertEqual(save.objects[210].relation_links, (0x0a,))
                self.assertEqual(save.objects[210].child_links, (0, 0, 0))
                self.assertEqual(save.objects[210].next_sibling, 0)
                self.assertEqual(save.objects[-1].file_offset + len(save.objects[-1].raw),
                                 34 + 20650)
                self.assertEqual(save.summary()['event_count'], len(events))
                self.assertEqual(len(save.summary(include_objects=True)['objects']), 734)

    def test_countdown_decreases_one_turn_between_two_saves(self):
        office = parse_gateway_save(fake_gateway_save(turns=0x0113,
                                                      events=((0x21, 145), (0x17, 446))))
        later = parse_gateway_save(fake_gateway_save(turns=0x0114,
                                                     events=((0x21, 144), (0x17, 445))))
        self.assertEqual(later.turn_counter - office.turn_counter, 1)
        for before, after in zip(office.events, later.events):
            self.assertEqual(before.event_id, after.event_id)
            self.assertEqual(before.remaining_turns - after.remaining_turns, 1)

    def test_reject_truncation_wrong_event_count_and_bad_header(self):
        good = fake_gateway_save()
        for invalid in (good[:-1], good + b'\0'):
            with self.assertRaisesRegex(ValueError, 'expected'):
                parse_gateway_save(invalid)
        with self.assertRaisesRegex(ValueError, 'truncated'):
            parse_gateway_save(b'A' * 100)
        invalid = good[:]
        invalid[33] = ord('X')
        with self.assertRaisesRegex(ValueError, 'NUL'):
            parse_gateway_save(invalid)
        invalid = good[:]
        invalid[0] = 0
        with self.assertRaisesRegex(ValueError, 'printable'):
            parse_gateway_save(invalid)
        invalid = good[:]
        invalid[:33] = b' ' * 33
        with self.assertRaisesRegex(ValueError, 'empty'):
            parse_gateway_save(invalid)
        invalid = good[:]
        struct.pack_into('<H', invalid, EVENTS_OFFSET - 2, 3)
        with self.assertRaisesRegex(ValueError, '3 scheduled events'):
            parse_gateway_save(invalid)

    def test_reject_impossible_handler_and_room_ids(self):
        invalid = fake_gateway_save()
        obj = parse_gateway_save(invalid).objects[210]
        struct.pack_into('<H', invalid, obj.file_offset + HANDLER_FIELDS[obj.kind - 1], 697)
        with self.assertRaisesRegex(ValueError, 'invalid native handler'):
            parse_gateway_save(invalid)
        invalid = fake_gateway_save()
        struct.pack_into('<H', invalid, DESCRIPTION_BYTES + OBJECT_BYTES, 735)
        with self.assertRaisesRegex(ValueError, 'invalid current/previous'):
            parse_gateway_save(invalid)


if __name__ == '__main__':
    unittest.main()
