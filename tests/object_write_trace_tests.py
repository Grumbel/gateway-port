"""Native object-record mutation report with synthetic hooked writes."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from summarize_object_writes import affected_child_slots, parse, summarize


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
        self.assertIn('28360  28s slot           0  0x000', with_setter)
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


if __name__ == '__main__':
    unittest.main()
