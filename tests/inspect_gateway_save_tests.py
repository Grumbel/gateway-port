"""Game-data-independent structural checks for original Gateway save files."""
import pathlib
import struct
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))

from inspect_gateway_save import (DESCRIPTION_BYTES, EVENT_BYTES, EVENTS_OFFSET,
                                  GLOBAL_STATE_BYTES, OBJECT_BYTES, OBJECT_KINDS,
                                  OBJECT_RECORD_SIZES, parse_gateway_save)
from inspect_object_state import HANDLER_FIELDS, LOCATION_FIELDS


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
