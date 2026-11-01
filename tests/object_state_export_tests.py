"""Synthetic snapshots exercise object records and both handler-table banks."""
import pathlib
import struct
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
from inspect_object_state import (child_links, inspect_objects, map_handler_entries,
                                  relation_links, HANDLER_FIELDS)
from inspect_child_chains import resolve_chains


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

    def test_handler_file_mapping_and_rejected_targets(self):
        objects = inspect_objects(self.memory, self.ds)
        manifest = dict(sections=[dict(id=0x26, runtime_segment=0x4990,
                        filename='TEST.OVL', image_file_offset=0x51480, file_bytes=0x100)])
        mapped = map_handler_entries(objects, manifest)
        self.assertEqual(mapped[0]['native_code']['file_offset'], 0x514b5)
        self.assertNotIn('native_code', mapped[2])
        self.assertNotIn('native_code', objects[0])
        manifest['sections'][0]['file_bytes'] = 0x35
        with self.assertRaises(ValueError):
            map_handler_entries(objects, manifest)
        with self.assertRaises(ValueError):
            map_handler_entries(objects, dict(sections=[]))
        with self.assertRaises(ValueError):
            map_handler_entries(objects, dict(sections=manifest['sections'] * 2))

    def test_changed_record_is_visible_in_export(self):
        before = inspect_objects(self.memory, self.ds)
        self.word(0x4000, 0x20e, 0xd3)
        after = inspect_objects(self.memory, self.ds)
        self.assertNotEqual(before, after)
        self.assertEqual(after[1]['location'], 0xd3)
        self.assertEqual(before[0], after[0])

    def test_relation_links_follow_original_type_specific_accessor(self):
        # First descriptor byte (kind*3-3) bounds the recursive relation
        # enumerator, unlike the third byte holding the record length.
        for kind, count in [(1, 2), (2, 2), (6, 4), (7, 1), (8, 1)]:
            self.memory[0x30000 + kind * 3 - 3] = count
        self.word(0x4000, 0x200 + 0x0e, 0x124)
        for index, relation in enumerate([0xd3, 0, 0x125, 0xee]):
            self.word(0x4000, 0x600 + 0x0a + 2 * index, relation)
        self.word(0x4000, 0x700 + 0x0c, 0x124)
        self.word(0x4000, 0x800 + 0x0c, 0x123)
        objects = inspect_objects(self.memory, self.ds)
        self.assertEqual(objects[0]['relation_links'], [])
        self.assertEqual(objects[1]['relation_links'], [0x124, 0x124])
        self.assertEqual(objects[5]['relation_links'], [0xd3, 0, 0x125, 0xee])
        self.assertEqual(objects[6]['relation_links'], [0x124])
        self.assertEqual(objects[7]['relation_links'], [0x123])
        self.assertEqual(objects[5]['location'], 0xd3)

    def test_invalid_type6_relation_count_is_rejected(self):
        self.memory[0x30000 + 6 * 3 - 3] = 8  # 0x0a + 16 > 24-byte record.
        with self.assertRaisesRegex(ValueError, 'relation list exceeds'):
            inspect_objects(self.memory, self.ds)
        self.assertEqual(relation_links(6, 0, b''), [])

    def test_child_links_use_separate_descriptor_slot_count_and_accessor(self):
        # EXE 0157:0825 uses descriptor byte kind*3-2, not kind*3-3.
        counts = {1: 4, 2: 4, 3: 3, 4: 3, 5: 4, 6: 4, 7: 4, 8: 3}
        offsets = {1: 0x0c, 2: 0x14, 4: 0x0a, 5: 0x0c, 6: 0x0e, 7: 0x12}
        for kind, count in counts.items():
            self.memory[0x30000 + kind * 3 - 2] = count
            if kind in offsets:
                for slot in range(1 if kind == 1 else count):
                    # Zero in the second slot ensures we don't drop holes.
                    value = 0 if slot == 1 else (kind << 8) | slot
                    self.word(0x4000, kind * 0x100 + offsets[kind] + 2 * slot, value)
        objects = inspect_objects(self.memory, self.ds)
        for kind, count in counts.items():
            item = objects[kind - 1]
            self.assertEqual(item['child_count'], count)
            if kind in (3, 8):
                self.assertEqual(item['child_links'], [])
            elif kind == 1:
                self.assertEqual(item['child_links'], [0x100] * count)
            else:
                self.assertEqual(item['child_links'],
                                 [0 if slot == 1 else (kind << 8) | slot
                                  for slot in range(count)])
            self.assertEqual(item['relation_count'], 0)
            self.assertEqual(item['relation_links'], [])

    def test_child_slot_bounds_are_checked(self):
        self.assertEqual(child_links(2, 0, b''), [])
        self.assertEqual(child_links(3, 100, b''), [])
        with self.assertRaisesRegex(ValueError, 'child list exceeds'):
            child_links(2, 9, bytes(36))
        with self.assertRaisesRegex(ValueError, 'child list exceeds'):
            child_links(1, 2, bytes(12))
        with self.assertRaisesRegex(ValueError, 'child list exceeds'):
            child_links(7, 8, bytes(32))
        # Type 1 checks only the one word it repeats, not count words.
        self.assertEqual(child_links(1, 30, bytes(14)), [0] * 30)
        self.memory[0x30000 + 6 * 3 - 2] = 9  # 0x0e + 18 > 24.
        with self.assertRaisesRegex(ValueError, 'child list exceeds'):
            inspect_objects(self.memory, self.ds)

    def test_linked_child_slot_and_next_sibling_have_separate_type_offsets(self):
        # The type-2 record uses +0x10/+0x12; types 7 and 8 use +0x0e/+0x10.
        # These are not the indexed child heads at +0x14/+0x12.
        for kind, slot_offset, next_offset in [(2, 0x10, 0x12),
                                                (7, 0x0e, 0x10),
                                                (8, 0x0e, 0x10)]:
            self.memory[0x40000 + kind * 0x100 + slot_offset] = kind
            self.word(0x4000, kind * 0x100 + next_offset, kind * 0x100 + 1)
        objects = inspect_objects(self.memory, self.ds)
        for kind in (2, 7, 8):
            self.assertEqual(objects[kind - 1]['child_slot'], kind)
            self.assertEqual(objects[kind - 1]['next_sibling'], kind * 0x100 + 1)
        for kind in (1, 3, 4, 5, 6):
            self.assertNotIn('child_slot', objects[kind - 1])
            self.assertNotIn('next_sibling', objects[kind - 1])

    def test_exported_indexed_head_resolves_through_a_real_sibling_field(self):
        # Two children under a type-2 parent: only the first occupies +0x14.
        # The second must be discovered through type-7's +0x10 sibling word.
        self.memory[0x30000 + 2 * 3 - 2] = 3
        self.word(0x4000, 0x200 + 0x14, 7)
        self.word(0x4000, 0x700 + 0x0c, 2)   # child 7's location
        self.word(0x4000, 0x700 + 0x10, 8)   # child 7's next sibling
        self.word(0x4000, 0x800 + 0x0c, 2)   # child 8's location
        objects = inspect_objects(self.memory, self.ds)
        report = resolve_chains(objects, 2)
        self.assertIn('0x007 (type 7)', report)
        self.assertIn('0x008 (type 8)', report)
        self.assertNotIn('!= parent', report)

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
