"""Synthetic snapshots exercise object records and both handler-table banks."""
import pathlib
import struct
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
from inspect_object_state import inspect_objects, HANDLER_FIELDS


class ObjectStateExportTests(unittest.TestCase):
    def setUp(self):
        self.memory = bytearray(0xa0000)
        self.ds = 0x1000
        for offset, segment in [(0x8d50, 0x2000), (0x8d52, 0x3000), (0x8d54, self.ds),
                                (0x8db4, 0x6000), (0x8db6, 0x6100)]:
            self.word(self.ds, offset, segment)
        self.word(self.ds, 0x38dc, 8)
        for kind, size in enumerate([26, 36, 12, 20, 22, 24, 32, 24], 1):
            self.memory[0x30000 + kind * 3 - 1] = size
            self.memory[0x20000 + 0xba + kind * 6] = kind
            self.word(0x2000, 0xbc + kind * 6, 0x100 * kind)
            self.word(0x2000, 0xbe + kind * 6, 0x4000)
            self.word(0x4000, 0x100 * kind, 1)
        for object_id, handler in [(1, 1), (2, 513), (8, 696)]:
            self.word(0x4000, 0x100 * object_id + HANDLER_FIELDS[object_id - 1], handler)
            bank, index = divmod(handler - 1, 512)
            self.word(0x6000 + bank * 0x100, index * 4, object_id * 0x10)
            self.word(0x6000 + bank * 0x100, index * 4 + 2, 0x7000)
            self.memory[0x70000 + object_id * 0x10:0x70000 + object_id * 0x10 + 10] = (
                bytes.fromhex('e80000ea') + struct.pack('<HHH', 0x35, 0x4990, 0x26))

    def word(self, segment, offset, value):
        struct.pack_into('<H', self.memory, segment * 16 + offset, value)

    def test_types_banks_names_and_section_words(self):
        self.word(0x4000, 0x102, 6)
        objects = inspect_objects(self.memory, self.ds, b'room\0alias\0')
        self.assertEqual(objects[0]['secondary_name'], 'alias')
        self.assertEqual(len(objects), 8)
        for index, handler in [(0, 1), (1, 513), (7, 696)]:
            item = objects[index]
            self.assertEqual(item['handler'], handler)
            self.assertEqual(item['handler_entry'], dict(segment=0x7000, offset=(index + 1) * 0x10))
            self.assertEqual(item['rtlink_stub']['section_word'], 0x26)
            self.assertEqual(item['name'], 'room')
        self.assertNotIn('handler_entry', objects[2])
        self.assertNotIn('location', objects[0])
        self.assertIn('location', objects[1])

    def test_changed_record_is_visible_in_export(self):
        before = inspect_objects(self.memory, self.ds)
        self.word(0x4000, 0x20e, 0xd3)
        after = inspect_objects(self.memory, self.ds)
        self.assertNotEqual(before, after)
        self.assertEqual(after[1]['location'], 0xd3)
        self.assertEqual(before[0], after[0])

    def test_bad_type_handler_size_name_and_pointer_are_rejected(self):
        for address, value in [(0x200c0, 9), (0x30002, 1)]:
            with self.subTest(address=address):
                old = self.memory[address]
                self.memory[address] = value
                with self.assertRaises(ValueError):
                    inspect_objects(self.memory, self.ds)
                self.memory[address] = old
        self.word(0x4000, 0x110, 697)
        with self.assertRaises(ValueError):
            inspect_objects(self.memory, self.ds)
        self.word(0x4000, 0x110, 1)
        with self.assertRaises(ValueError):
            inspect_objects(self.memory, self.ds, b'no terminator')
        self.word(0x2000, 0xc2, 0xffff)
        self.word(0x2000, 0xc4, 0x9fff)
        with self.assertRaises(ValueError):
            inspect_objects(self.memory, self.ds)


if __name__ == '__main__':
    unittest.main()
