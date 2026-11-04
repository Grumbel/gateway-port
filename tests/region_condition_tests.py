# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Synthetic checks for original overlay conditional-region records."""

import pathlib
import struct
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
from inspect_region_conditions import (
    EXE_DATA_BASE, EXE_SHA256, inspect, live_picker, report,
    scan_constant_tables, scan_report, scan_resident_ds_calls)


class RegionConditionTests(unittest.TestCase):
    def setUp(self):
        self.manifest = {
            'sections': [
                {'id': 38, 'linked_segment': 0x39d8,
                 'image_file_offset': 0x20, 'file_bytes': 0x80},
                # Overlapping linked sections must not steal this section's
                # own callbacks merely because their addresses also fit.
                {'id': 39, 'linked_segment': 0x39d8,
                 'image_file_offset': 0xa0, 'file_bytes': 0x80},
            ],
        }
        data = bytearray(0x120)
        struct.pack_into('<5H', data, 0x50, 0x124, 0, 29, 0x1b, 0x39d8)
        struct.pack_into('<5H', data, 0x5a, 0x124, 0, 28, 0x0a, 0x39d8)
        struct.pack_into('<5H', data, 0x64, 0x126, 0x125, 0, 0, 0)
        self.data = bytes(data)

    def test_native_record_format_and_section_scoped_callbacks(self):
        records = inspect(self.data, self.manifest, 38, 0x30, 3)
        self.assertEqual([r['file_offset'] for r in records],
                         [0x50, 0x5a, 0x64])
        self.assertEqual([r['slot'] for r in records], [29, 28, 0])
        self.assertEqual([r['relation_id'] for r in records], [0, 0, 0x125])
        self.assertEqual([r['callback_file'] for r in records],
                         [(38, 0x3b), (38, 0x2a), None])
        text = report(self.data, self.manifest, 38, 0x30, 3)
        self.assertIn('selector=slot 29', text)
        self.assertIn('immediate relation to 0x125', text)
        self.assertIn('ovl:0x00003b, section 38', text)
        self.assertIn('no claim of rendered visibility', text)

    def test_rejects_ambiguous_and_out_of_bounds_tables(self):
        with self.assertRaisesRegex(ValueError, 'outside initialized section'):
            inspect(self.data, self.manifest, 38, 0x78, 2)
        with self.assertRaisesRegex(ValueError, 'outside initialized section'):
            inspect(self.data, self.manifest, 38, 0x30, 0)
        with self.assertRaisesRegex(ValueError, 'absent or duplicated'):
            inspect(self.data, self.manifest, 37, 0x30, 1)
        duplicated = {'sections': self.manifest['sections'] +
                       [self.manifest['sections'][0]]}
        with self.assertRaisesRegex(ValueError, 'absent or duplicated'):
            inspect(self.data, duplicated, 38, 0x30, 1)
        with self.assertRaisesRegex(ValueError, 'past original OVL file'):
            inspect(self.data[:0x55], self.manifest, 38, 0x30, 1)

    def test_original_runtime_slot_table_and_dynamic_mask(self):
        records = inspect(self.data, self.manifest, 38, 0x30, 3)
        memory = bytearray(0x20000)
        ds = 0x1000
        base = ds * 16
        struct.pack_into('<H', memory, base + 0xa6ce, 1)  # active group 1
        memory[base + 0xa8d5] = 31
        for slot in range(31):
            memory[base + 0xa974 + 35 + slot] = slot + 26
        struct.pack_into('<H', memory, base + 0xa8da + 60, 0x126)
        memory[base + 0xa920 + 55] = 0x80
        group, states = live_picker(memory, ds, records)
        self.assertEqual(group, 1)
        self.assertEqual(states, [[(29, 55, 0x80)], [(28, 54, 0)],
                                  [(30, 56, 0)]])
        memory[base + 0xa920 + 55] = 0
        memory[base + 0xa920 + 54] = 0x80
        _, states = live_picker(memory, ds, records)
        self.assertEqual(states[0][0][2], 0)
        self.assertEqual(states[1][0][2], 0x80)
        with self.assertRaisesRegex(ValueError, 'outside guest RAM'):
            live_picker(memory[:0x15000], ds, records)

    def test_constant_argument_calls_discover_tables(self):
        image = bytearray(self.data)
        call = 0x90
        image[call - 12] = 0xb8  # mov ax,3 (record count)
        struct.pack_into('<H', image, call - 11, 3)
        image[call - 9] = 0x50  # push ax
        image[call - 8] = 0xb8  # mov ax,0x30 (image-relative offset)
        struct.pack_into('<H', image, call - 7, 0x30)
        image[call - 5] = 0xba  # mov dx,0x39d8 (linked section)
        struct.pack_into('<H', image, call - 4, 0x39d8)
        image[call - 2:call] = bytes((0x52, 0x50))
        struct.pack_into('<BHH', image, call, 0x9a, 0x4251, 0x1c24)
        # A second call without the fixed immediate prologue must be counted
        # but must not invent a table.
        struct.pack_into('<BHH', image, 0xf0, 0x9a, 0x4251, 0x1c24)
        calls, immediate, tables = scan_constant_tables(image, self.manifest)
        self.assertEqual((calls, immediate, len(tables)), (2, 1, 1))
        self.assertEqual((tables[0]['call_file_offset'], tables[0]['table_section'],
                          tables[0]['table_file_offset'], tables[0]['count']),
                         (0x90, 38, 0x50, 3))
        text = scan_report(image, self.manifest)
        self.assertIn('2 direct evaluator calls', text)
        self.assertIn('3 plausible overlay rule records', text)

    def test_resident_ds_one_record_compiler_form(self):
        image = bytearray(self.data)
        call = 0x90
        image[call - 9] = 0xb8  # mov ax,1; push ax
        struct.pack_into('<H', image, call - 8, 1)
        image[call - 6:call - 4] = bytes((0x50, 0xb8))
        struct.pack_into('<H', image, call - 4, 0x20)  # resident DS offset
        image[call - 2:call] = bytes((0x1e, 0x50))  # push ds; push ax
        struct.pack_into('<BHH', image, call, 0x9a, 0x4251, 0x1c24)
        file_offset = EXE_DATA_BASE + 0x20
        resident_exe = bytearray(file_offset + 10)
        struct.pack_into('<5H', resident_exe, file_offset,
                         0x126, 0x125, 0, 0, 0)
        forms, tables = scan_resident_ds_calls(image, self.manifest, resident_exe)
        self.assertEqual((forms, len(tables)), (1, 1))
        self.assertEqual((tables[0]['ds_offset'], tables[0]['object_id'],
                          tables[0]['relation_id'], tables[0]['table_file_offset']),
                         (0x20, 0x126, 0x125, file_offset))
        result = scan_report(image, self.manifest, exe=resident_exe)
        self.assertIn('1 DS-pointer forms, 1 validated EXE references', result)
        self.assertIn('1 of 1 direct calls validated', result)
        struct.pack_into('<5H', resident_exe, file_offset,
                         0x0, 0x125, 0, 0, 0)
        _, rejected = scan_resident_ds_calls(image, self.manifest, resident_exe)
        self.assertEqual(rejected, [])

    def test_optional_pinned_original_exe_resident_records(self):
        exe_path = (pathlib.Path(__file__).resolve().parents[2] /
                    'gateway-dos/GATEMAIN.EXE')
        if not exe_path.exists():
            self.skipTest('original Gateway EXE is not installed')
        import hashlib
        exe = exe_path.read_bytes()
        self.assertEqual(hashlib.sha256(exe).hexdigest(), EXE_SHA256)
        # Independently confirmed against the original running game's DS
        # records. This is a fixed, version-specific resident address map.
        cases = ((0x4914, 0x0d6, 0x1b1), (0x4a3a, 0x266, 0x290),
                 (0x6238, 0x085, 0x071), (0x80d4, 0x00d, 0x00a))
        for ds_offset, obj, rel in cases:
            self.assertEqual(struct.unpack_from('<5H', exe,
                             EXE_DATA_BASE + ds_offset),
                             (obj, rel, 0, 0, 0))
        self.assertEqual(struct.unpack_from('<5H', exe,
                         EXE_DATA_BASE + 0x60c0),
                         (0x86, 0, 0, 0x4495, 0x1c24))

    def test_optional_original_gateway_room_rules(self):
        original = pathlib.Path(__file__).resolve().parents[2] / 'gateway-dos/GATEMAIN.OVL'
        if not original.exists():
            self.skipTest('original Gateway game data is not installed')
        # Verified section-38 header values; actual data is read from the
        # unmodified external OVL and is never copied into this test.
        manifest = {'sections': [
            {'id': 38, 'linked_segment': 0x39d8,
             'image_file_offset': 0x51480, 'file_bytes': 10592},
        ]}
        records = inspect(original.read_bytes(), manifest, 38, 0x292a, 3)
        self.assertEqual([(r['object_id'], r['relation_id'], r['slot'],
                           r['callback_file']) for r in records],
                         [(0x124, 0, 29, (38, 0x5149b)),
                          (0x124, 0, 28, (38, 0x5148a)),
                          (0x126, 0x125, 0, None)])


if __name__ == '__main__':
    unittest.main()
