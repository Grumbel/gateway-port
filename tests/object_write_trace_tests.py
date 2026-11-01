"""Native object-record mutation report with synthetic hooked writes."""
from pathlib import Path
import json
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from summarize_object_writes import (affected_child_slots, head_relocation_pairs,
                                     map_child_setter_return, parse, summarize,
                                     child_setter_calls)


TOOL = Path(__file__).resolve().parents[1] / 'tools' / 'summarize_object_writes.py'
HEADER = 'object-watch ms=11000 object=124 type=2 at=5b46:1234 size=26 child_count=3'
WRITES = [
    'object-write ms=18393 object=124 pc=47123 cs=4661 ip=b13 '
    'offset=c size=2 before=0000 value=0100',
    'object-write ms=28360 object=124 pc=483c0 cs=4661 ip=1db0 '
    'offset=14 size=2 before=2301 value=0000',
    'object-write ms=28361 object=124 pc=483c0 cs=4661 ip=1db0 '
    'offset=14 size=2 before=0000 value=0000',
    'object-write ms=31360 object=124 pc=483d0 cs=4661 ip=1dc0 '
    'offset=16 size=2 before=0000 value=2301',
]
SETTER = ('entry ms=28360 pc=11e6c cs=1157 stack=2222,3333,124,0,0 '
          'child_set_object=124 child_set_slot=0 child_set_value=0')
DEST_SETTER = ('entry ms=28360 pc=11e6c cs=1157 stack=171d,4319,d3,0,123 '
               'child_set_object=d3 child_set_slot=0 child_set_value=123')


class ObjectWriteTraceTests(unittest.TestCase):
    def test_native_offset_classification_and_pre_write_filter(self):
        watch, writes = parse([HEADER, *WRITES])
        self.assertEqual(watch['type'], 2)
        self.assertEqual(len(writes), 4)
        self.assertEqual(affected_child_slots(watch, 0x0c, 2), [])
        self.assertEqual(affected_child_slots(watch, 0x14, 2), [0])
        self.assertEqual(affected_child_slots(watch, 0x15, 2), [0, 1])
        report = summarize([HEADER, *WRITES], child_only=True)
        self.assertNotIn('18393', report)
        self.assertIn('28360', report)
        self.assertIn('2301 -> 0000  0', report)
        self.assertIn('31360', report)
        self.assertIn('0000 -> 2301  1', report)
        self.assertNotIn('28361', report)
        self.assertIn('before execution', report)
        with_setter = summarize([HEADER, SETTER, *WRITES], child_only=True)
        self.assertIn('Native indexed-child setter', with_setter)
        self.assertIn('28360  28s slot        0x124     0  0x000', with_setter)
        self.assertIn('returns to', with_setter)
        self.assertIn('3333:2222', with_setter)
        full = summarize([HEADER, *WRITES], changed_only=False)
        self.assertIn('18393', full)
        self.assertIn('28361', full)

    def test_repeated_and_empty_child_accessors(self):
        watch = dict(type=1, child_count=3)
        self.assertEqual(affected_child_slots(watch, 0x0c, 1), [0])
        watch['type'] = 8
        self.assertEqual(affected_child_slots(watch, 0x0c, 2), [])
        watch['type'] = 6
        self.assertEqual(affected_child_slots(watch, 0x10, 2), [1])

    def test_repeated_book_moves_restore_room_next_sibling(self):
        # The same pairing shape as the observed 28s/31s/34s seven-command
        # replay, including startup relocations whose action is unknown.
        manifest = dict(sections=[dict(id=4, runtime_segment=0x4319,
                                       image_file_offset=0xb1e0, file_bytes=0x1ac0,
                                       filename='GATEMAIN.OVL')])
        overlay = bytearray(0xd000)
        for address in (0xc879, 0xc8f8):
            overlay[address:address + 5] = bytes.fromhex('9afc085701')

        def setter(ms, object_id, value, insertion=False):
            saved_ip = '171d' if insertion else '169e'
            return (f'entry ms={ms} pc=11e6c cs=1157 '
                    f'stack={saved_ip},4319,{object_id:x},0,{value:x} '
                    f'child_set_object={object_id:x} child_set_slot=0 '
                    f'child_set_value={value:x}')

        moves = [(5408, 0x161, 0, 0x161, 0x16d),
                 (28356, 0x124, 0, 0xd3, 0x123),
                 (31333, 0xd3, 0, 0xee, 0x123),
                 (34354, 0xee, 0x175, 0xd3, 0x123)]
        lines = [line for ms, old_parent, remainder, new_parent, moved in moves
                 for line in (setter(ms, old_parent, remainder),
                              setter(ms, new_parent, moved, insertion=True))]
        paired = head_relocation_pairs(child_setter_calls(lines), manifest, overlay)
        self.assertEqual(len(paired), 4)
        self.assertEqual([row['object'] for row in paired],
                         [0x16d, 0x123, 0x123, 0x123])
        self.assertEqual([(row['old_parent'], row['replacement']) for row in paired],
                         [(0x161, 0), (0x124, 0), (0xd3, 0), (0xee, 0x175)])
        report = summarize([HEADER, *lines], manifest=manifest, overlay=overlay,
                           relocations=True)
        self.assertIn('0x123  0x0ee', report)
        self.assertIn('0x175', report)

    def test_original_direct_call_is_resolved_only_with_rtlink_bytes(self):
        # Several sections occupy 4319:xxxx. Only section 4 contains the
        # original CALL 0157:08fc immediately before the captured return.
        manifest = {'sections': [
            dict(id=3, runtime_segment=0x4319, image_file_offset=0x9300,
                 file_bytes=0x1c80, filename='GATEMAIN.OVL'),
            dict(id=4, runtime_segment=0x4319, image_file_offset=0xb1e0,
                 file_bytes=0x1ac0, filename='GATEMAIN.OVL'),
        ]}
        overlay = bytearray(0xd000)
        overlay[0xc879:0xc87e] = bytes.fromhex('9afc085701')
        overlay[0xc8f8:0xc8fd] = bytes.fromhex('9afc085701')
        self.assertEqual(map_child_setter_return('4319:169e', manifest, overlay),
                         (4, 0xc879))
        self.assertEqual(map_child_setter_return('4319:171d', manifest, overlay),
                         (4, 0xc8f8))
        self.assertIn('section=4 GATEMAIN.OVL:0xc879',
                      summarize([HEADER, SETTER.replace('stack=2222,3333',
                                                       'stack=169e,4319'), *WRITES],
                                manifest=manifest, overlay=overlay))
        all_calls = summarize([HEADER, SETTER.replace('stack=2222,3333',
                                                     'stack=169e,4319'),
                               DEST_SETTER, *WRITES],
                              all_setters=True, manifest=manifest, overlay=overlay)
        self.assertIn('0x0d3', all_calls)
        self.assertIn('section=4 GATEMAIN.OVL:0xc8f8', all_calls)
        paired_calls = [SETTER.replace('stack=2222,3333', 'stack=169e,4319'),
                        DEST_SETTER]
        self.assertEqual(head_relocation_pairs(child_setter_calls(paired_calls),
                                               manifest, overlay), [
            dict(ms=28360, object=0x123, old_parent=0x124, old_slot=0,
                 replacement=0, new_parent=0xd3, new_slot=0)])
        move_report = summarize([HEADER, *paired_calls, *WRITES],
                                relocations=True, manifest=manifest, overlay=overlay)
        self.assertIn('inferred moves', move_report)
        self.assertIn('0x123  0x124', move_report)
        self.assertIn('0x0d3', move_report)
        self.assertIn('post-write state', move_report)
        self.assertEqual(head_relocation_pairs(child_setter_calls(paired_calls[::-1]),
                                               manifest, overlay), [])
        late_destination = DEST_SETTER.replace('ms=28360', 'ms=28365')
        self.assertEqual(head_relocation_pairs(child_setter_calls([paired_calls[0],
                                                                  late_destination]),
                                               manifest, overlay), [])
        self.assertNotIn('0x0d3', summarize([HEADER, DEST_SETTER, *WRITES]))
        overlay[0xc879] = 0x90
        self.assertIsNone(map_child_setter_return('4319:169e', manifest, overlay))
        with self.assertRaisesRegex(ValueError, 'supplied together'):
            summarize([HEADER], manifest=manifest)
        with self.assertRaisesRegex(ValueError, 'requires RTLink manifest'):
            summarize([HEADER], relocations=True)
        # Duplicate matching section must be reported, not guessed.
        overlay[0xc879] = 0x9a
        manifest['sections'].append(dict(id=5, runtime_segment=0x4319,
                                         image_file_offset=0xb1e0, file_bytes=0x1ac0,
                                         filename='GATEMAIN.OVL'))
        with self.assertRaisesRegex(ValueError, 'ambiguous'):
            map_child_setter_return('4319:169e', manifest, overlay)

    def test_rejects_bogus_hook_and_inconsistent_record(self):
        with self.assertRaisesRegex(ValueError, 'before object-watch'):
            parse(WRITES[:1])
        with self.assertRaisesRegex(ValueError, 'multiple object-watch'):
            parse([HEADER, HEADER])
        with self.assertRaisesRegex(ValueError, 'invalid object-write record'):
            parse([HEADER, WRITES[0].replace('object=124', 'object=123')])
        with self.assertRaisesRegex(ValueError, 'invalid object-write hexadecimal'):
            parse([HEADER, WRITES[0].replace('before=0000', 'before=xxxx')])

    def test_cli_command_labels_and_invalid_trace(self):
        with tempfile.TemporaryDirectory() as directory:
            trace = Path(directory) / 'native.trace'
            trace.write_text('\n'.join([HEADER, *WRITES]))
            labels = 'open drawer,close drawer,open drawer,take book,drop book,take book,inventory'
            command = [sys.executable, str(TOOL), str(trace), '--child-only',
                       '--commands', labels]
            success = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(success.returncode, 0, success.stderr)
            self.assertIn('take book', success.stdout)
            trace.write_text('no object-watch entries')
            missing = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(missing.returncode, 2)
            self.assertIn('no object-watch entry', missing.stderr)

    def test_cli_requires_both_overlay_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            trace = Path(directory) / 'native.trace'
            trace.write_text('\n'.join([HEADER, SETTER, *WRITES]))
            manifest = Path(directory) / 'manifest.json'
            manifest.write_text(json.dumps({'sections': []}))
            result = subprocess.run([sys.executable, str(TOOL), str(trace),
                                     '--overlay-map', str(manifest)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertIn('--overlay-map and --overlay-file', result.stderr)


if __name__ == '__main__':
    unittest.main()
