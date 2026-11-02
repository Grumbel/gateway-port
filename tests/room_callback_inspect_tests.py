"""Asset-independent tests for the native room-exit callback directory."""

from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from inspect_room_callbacks import inspect_callbacks, format_text
from rtlink_trampoline import decode_trampoline, map_reference, sections_by_id


class RoomCallbackTests(unittest.TestCase):
    def setUp(self):
        self.memory = bytearray(0xa0000)
        self.ds = 0x6000
        self.directory = 0x3000
        self.index = 0x14
        self.stub_offset = 0x1200
        struct.pack_into('<H', self.memory, self.ds * 16 + 0x8da6, self.directory)
        struct.pack_into('<HH', self.memory, self.directory * 16 + 0x4a22 + 4*self.index,
                         self.stub_offset, 0x2c24)
        self.stub_address = 0x2c24 * 16 + self.stub_offset
        self.stub = struct.pack('<BhBHHH', 0xe8,
                                0x058d - self.stub_offset - 3, 0xea,
                                0x10, 0x4000, 0)
        self.memory[self.stub_address:self.stub_address+10] = self.stub
        self.world = {'rooms': [dict(id=0xf5, name='Casino', exits=[
            dict(kind=4, argument=self.index, direction_code=3, index=265),
            dict(kind=1, argument=0xf4, direction_code=7, index=267)])]}
        self.manifest = dict(sections=[dict(id=0, filename='GATEMAIN.OVL',
                                            runtime_segment=0x4000,
                                            file_bytes=128, image_file_offset=0x100)])

    def test_maps_entry_from_far_pointer_to_original_code(self):
        report = inspect_callbacks(self.memory, self.ds, self.world, self.manifest)
        self.assertEqual(report['table_count'], 44)
        self.assertEqual(report['exit_count'], 1)
        self.assertEqual(len(report['callbacks']), 1)
        entry = report['callbacks'][0]
        self.assertEqual(entry['callback_id'], 0x14)
        self.assertEqual(entry['pointer_segment'], 0x2c24)
        self.assertEqual(entry['pointer_offset'], 0x1200)
        self.assertEqual(entry['rtlink_reference'],
                         dict(section=0, target_segment=0x4000, target_offset=0x10))
        self.assertEqual(entry['native_code'], dict(filename='GATEMAIN.OVL', section=0,
                                                     image_offset=0x10, file_offset=0x110))
        self.assertIn('Casino', format_text(report))

    def test_without_manifest_preserves_reference_but_not_guessed_file_offset(self):
        entry = inspect_callbacks(self.memory, self.ds, self.world)['callbacks'][0]
        self.assertEqual(entry['status'], 'rtlink')
        self.assertNotIn('native_code', entry)

    def test_duplicate_use_of_one_callback_is_kept(self):
        self.world['rooms'][0]['exits'].append(dict(kind=4, argument=self.index,
                                                   direction_code=11, index=266))
        report = inspect_callbacks(self.memory, self.ds, self.world)
        self.assertEqual(len(report['callbacks']), 1)
        self.assertEqual(report['exit_count'], 2)
        self.assertEqual(report['callbacks'][0]['uses'][1]['direction_code'], 11)

    def test_out_of_table_argument_is_explicit(self):
        self.world['rooms'][0]['exits'][0]['argument'] = 44
        entry = inspect_callbacks(self.memory, self.ds, self.world)['callbacks'][0]
        self.assertIn('outside native dispatch table', entry['status'])
        self.assertNotIn('pointer_segment', entry)

    def test_unverified_trampoline_is_not_mapped(self):
        self.memory[self.stub_address] = 0x90
        entry = inspect_callbacks(self.memory, self.ds, self.world, self.manifest)['callbacks'][0]
        self.assertIn('not a verified RTLink', entry['status'])
        self.assertNotIn('native_code', entry)

    def test_invalid_section_rejected(self):
        struct.pack_into('<H', self.memory, self.stub_address+8, 58)
        with self.assertRaisesRegex(ValueError, 'invalid section ID'):
            inspect_callbacks(self.memory, self.ds, self.world)

    def test_null_and_invalid_directory(self):
        struct.pack_into('<H', self.memory, self.ds*16+0x8da6, 0)
        with self.assertRaisesRegex(ValueError, 'table pointer is null'):
            inspect_callbacks(self.memory, self.ds, self.world)
        with self.assertRaisesRegex(ValueError, 'data segment'):
            inspect_callbacks(self.memory, 0, self.world)

    def test_rejects_target_outside_section(self):
        struct.pack_into('<H', self.memory, self.stub_address+4, 128)
        with self.assertRaisesRegex(ValueError, 'outside section'):
            inspect_callbacks(self.memory, self.ds, self.world, self.manifest)

    def test_strict_rtlink_validation(self):
        self.assertIsNone(decode_trampoline(self.memory, 0x2c23, self.stub_offset))
        self.assertIsNone(decode_trampoline(self.memory, 0x2c24, 0xffff))
        self.assertEqual(map_reference(dict(section=0, target_segment=0x4000,
                                            target_offset=0x10),
                                       sections_by_id(self.manifest))['file_offset'], 0x110)
        with self.assertRaisesRegex(ValueError, 'duplicate RTLink'):
            sections_by_id(dict(sections=self.manifest['sections'] * 2))


if __name__ == '__main__':
    unittest.main()
