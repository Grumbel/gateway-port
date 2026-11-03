# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Synthetic evidence fixtures: never require original Gateway assets."""
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from inspect_rtlink_events import parse_events
from inventory_native_handlers import parse_calls
from build_handler_evidence import build_index


ENTER = ('rtlink-enter id=1 ms=100 section=48 target=4990:1a19 '
         'caller=1474:1e ss=5b46 sp=db5a ax=f bx=50 cx=2 dx=343b '
         'args=f,dbb6,79c,148e object_state=verified')
CHANGE = 'rtlink-change id=1 object=1ad byte=8 before=c after=0'
RETURN = ('rtlink-return id=1 ms=110 ax=0 bx=1537 cx=603 dx=353f '
          'ss=5b46 sp=db5a object_bytes_changed=1 object_state=verified')
FOOTER = 'rtlink-events selected=1 returned=1 unfinished=0 limit=256'
CENSUS = ['rtlink-census manager_hits=3 validated=3 unrecognized=0',
          'rtlink-call section=48 target=4990:1a19 count=2 first_ms=100 last_ms=105',
          'rtlink-call section=38 target=49d8:1db0 count=1 first_ms=101 last_ms=101']


def entry(section, segment, ip, offset):
    return dict(section=section, target_segment=segment, target_offset=ip,
                file_offset=offset, image_offset=0)


class RtlinkEventParserTests(unittest.TestCase):
    def test_paired_return_and_record_delta(self):
        events, summary = parse_events([ENTER, CHANGE, RETURN, FOOTER])
        self.assertEqual(summary['returned'], 1)
        self.assertEqual(events[0]['target'], (0x4990, 0x1a19))
        self.assertEqual(events[0]['args'][0], 0xf)
        self.assertEqual(events[0]['changes'], [dict(object=0x1ad, byte=8,
                                                     before=0xc, after=0)])
        self.assertEqual(events[0]['ax_out'], 0)

    def test_unfinished_event_is_explicit(self):
        events, summary = parse_events([ENTER,
            'rtlink-events selected=1 returned=0 unfinished=1 limit=256'])
        self.assertFalse(events[0]['returned'])
        self.assertEqual(summary['unfinished'], 1)

    def test_rejects_orphan_duplicate_and_lost_changes(self):
        for lines in ([RETURN, FOOTER], [ENTER, RETURN, RETURN, FOOTER],
                      [ENTER, CHANGE, CHANGE, RETURN, FOOTER],
                      [ENTER, CHANGE, RETURN.replace('object_bytes_changed=1',
                                                      'object_bytes_changed=0'), FOOTER],
                      [ENTER, CHANGE, RETURN.replace('object_state=verified',
                                                      'object_state=unavailable'), FOOTER]):
            with self.subTest(lines=lines), self.assertRaises(ValueError):
                parse_events(lines)

    def test_rejects_wrong_schema_or_footer(self):
        for lines in ([ENTER, CHANGE, RETURN],
                      [ENTER, CHANGE, RETURN, FOOTER.replace('returned=1', 'returned=0')],
                      [ENTER.replace('section=48', 'section=58'), FOOTER],
                      [ENTER.replace('target=4990:1a19', 'target=4990'), FOOTER],
                      [ENTER.replace('args=f,dbb6,79c,148e', 'args=f'), FOOTER],
                      [ENTER.replace('object_state=verified', 'object_state=probably'), FOOTER]):
            with self.subTest(lines=lines), self.assertRaises(ValueError):
                parse_events(lines)


class EvidenceIndexTests(unittest.TestCase):
    def setUp(self):
        self.inventory = dict(schema_version=1, handlers=[dict(
            handler=283, entry=entry(38, 0x49d8, 0x1db0, 0x51a78),
            objects=[dict(id=0x124, type=7, name='drawer')])],
            callbacks=[dict(callback_id=20, uses=1,
                            entry=entry(48, 0x4990, 0x1a19, 0x665f9))],
            unclassified_rtlink_targets=[])
        self.graph = dict(schema_version=1, targets=[dict(
            name='gw_casino', role='callback-entry', origin='GATEMAIN.OVL:0x665f9',
            function_entry='GW_RT_048::4990:1a19',
            edges=[dict(callsite='GW_RT_048::4990:1a21',
                        destination_ovl_file_offset=0x51a78)],
            unresolved_calls=['GW_RT_048::4990:1a28'])])
        calls, header = parse_calls(CENSUS)
        events, summary = parse_events([ENTER, CHANGE, RETURN, FOOTER])
        self.replays = [('casino', calls, header, events, summary)]

    def test_combines_section_qualified_runtime_and_static_evidence(self):
        result = build_index(self.inventory, self.graph, self.replays)
        self.assertEqual(result['summary']['handler_entries'], 1)
        self.assertEqual(result['summary']['room_callbacks'], 1)
        self.assertEqual(result['summary']['sampled_events'], 1)
        self.assertEqual(result['summary']['observed_known_entries'], 2)
        casino = next(item for item in result['targets'] if item['kind'] == 'room-callback')
        self.assertEqual(casino['observed_rtlink_calls'], 2)
        self.assertEqual(casino['changed_objects'], [0x1ad])
        self.assertEqual(casino['native_samples'][0]['ax_out'], 0)
        self.assertEqual(casino['ghidra_contexts'][0]['unresolved_call_sites'], 1)
        drawer = next(item for item in result['targets'] if item['kind'] == 'object-handler')
        self.assertEqual(drawer['static_incoming'][0]['source'], 'gw_casino')
        self.assertEqual(drawer['observed_rtlink_calls'], 1)

    def test_rejects_samples_larger_than_census(self):
        bad_calls = {(48, 0x4990, 0x1a19): dict(calls=0)}
        bad = [('casino', bad_calls, {}, self.replays[0][3], self.replays[0][4])]
        with self.assertRaisesRegex(ValueError, 'exceed'):
            build_index(self.inventory, self.graph, bad)

    def test_unclassified_trampoline_not_labeled_as_handler(self):
        calls, header = parse_calls([
            'rtlink-census manager_hits=1 validated=1 unrecognized=0',
            'rtlink-call section=0 target=4990:7e count=1 first_ms=1 last_ms=1'])
        result = build_index(self.inventory, self.graph, [
            ('sample', calls, header, [], dict(selected=0, returned=0, unfinished=0))])
        self.assertEqual(result['summary']['unknown_census_targets'], 1)
        self.assertEqual(result['unknown_rtlink_targets'][0]['section'], 0)

    def test_rejects_ambiguous_entry_mapping(self):
        self.inventory['callbacks'][0]['entry']['file_offset'] = 0x51a78
        with self.assertRaisesRegex(ValueError, 'duplicate original OVL offset'):
            build_index(self.inventory, self.graph, self.replays)

    def test_unseen_does_not_become_unsupported(self):
        result = build_index(self.inventory, self.graph, [])
        self.assertEqual(result['summary']['observed_known_entries'], 0)
        self.assertEqual(result['targets'][0]['observed_rtlink_calls'], 0)
        self.assertEqual(result['targets'][0]['native_samples'], [])


if __name__ == '__main__':
    unittest.main()
