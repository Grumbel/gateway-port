# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Synthetic and optional original-game tests for 14-byte picture layers."""

import pathlib
import struct
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from inspect_picture_conditions import (
    decode_record, inspect, live_cache, plausible, report, scan_calls,
    scan_report)
from inspect_region_conditions import EXE_DATA_BASE


class PictureConditionTests(unittest.TestCase):
    def setUp(self):
        self.manifest = {'sections': [
            {'id': 38, 'linked_segment': 0x39d8,
             'image_file_offset': 0x20, 'file_bytes': 0x120},
        ]}
        data = bytearray(0x140)
        struct.pack_into('<7H', data, 0x50, 0x124, 0, 0x210,
                         0x0c, 0, 0, 0)
        struct.pack_into('<7H', data, 0x5e, 0x126, 0x125, 0x212,
                         0, 0, 0, 0)
        struct.pack_into('<7H', data, 0x6c, 0x131, 0, 0x211,
                         0, 0, 0, 0x39d8)
        self.data = bytes(data)

    def test_decode_and_stable_callback_mapping(self):
        records = inspect(self.data, self.manifest, 38, 0x30, 3)
        self.assertEqual([(r['object_id'], r['picture_id'], r['property'])
                          for r in records],
                         [(0x124, 0x210, 0x0c),
                          (0x126, 0x212, 0), (0x131, 0x211, 0)])
        self.assertEqual(records[2]['callback_file'], (38, 0x20))
        self.assertTrue(all(plausible(r) for r in records))
        text = report(self.data, self.manifest, 38, 0x30, 3)
        self.assertIn('picture=0x0210 property=0x0c', text)
        self.assertIn('callback=39d8:0000 (ovl:0x000020 section 38)', text)

    def test_unconditional_records_and_bounds(self):
        image = bytearray(self.data)
        struct.pack_into('<7H', image, 0x50, 0, 0, 0x123,
                         0, 0, 0, 0)
        self.assertTrue(plausible(decode_record(image, 0x50)))
        struct.pack_into('<H', image, 0x50, 0xffff)
        self.assertFalse(plausible(decode_record(image, 0x50)))
        with self.assertRaisesRegex(ValueError, 'outside section'):
            inspect(self.data, self.manifest, 38, 0x118, 2)
        with self.assertRaisesRegex(ValueError, 'outside section'):
            inspect(self.data, self.manifest, 38, 0x30, 0)

    def test_overlay_and_resident_call_forms(self):
        image = bytearray(self.data)
        call = 0x90
        image[call-12:call] = bytes((0xb8, 3, 0, 0x50, 0xb8,
                                     0x30, 0, 0xba, 0xd8, 0x39,
                                     0x52, 0x50))
        struct.pack_into('<BHH', image, call, 0x9a, 0x425b, 0x1c24)
        call = 0xe0
        image[call-9:call] = bytes((0xb8, 1, 0, 0x50, 0xb8,
                                   0x20, 0, 0x1e, 0x50))
        struct.pack_into('<BHH', image, call, 0x9a, 0x425b, 0x1c24)
        source = bytearray(EXE_DATA_BASE + 0x20 + 14)
        struct.pack_into('<7H', source, EXE_DATA_BASE + 0x20,
                         0, 0, 0x220, 0, 0, 0, 0)
        calls, ovl_forms, ds_forms, overlay, resident = \
            scan_calls(image, self.manifest, source)
        self.assertEqual((calls, ovl_forms, ds_forms), (2, 1, 1))
        self.assertEqual(len(overlay), 1)
        self.assertEqual(len(resident), 1)
        self.assertEqual(overlay[0]['count'], 3)
        self.assertEqual(resident[0]['records'][0]['picture_id'], 0x220)
        self.assertIn('2 of 2 calls validated',
                      scan_report(image, self.manifest, exe=source))

    def test_optional_original_quest_room_table_and_live_layer_cache(self):
        original = ROOT.parent / 'gateway-dos/GATEMAIN.OVL'
        if not original.exists():
            self.skipTest('original Gateway OVL not installed')
        manifest = {'sections': [{
            'id': 38, 'linked_segment': 0x39d8,
            'image_file_offset': 0x51480, 'file_bytes': 10592,
        }]}
        records = inspect(original.read_bytes(), manifest, 38, 0x2900, 3)
        self.assertEqual([(r['object_id'], r['relation_id'],
                           r['picture_id'], r['property'],
                           r['callback_file']) for r in records],
                         [(0x124, 0, 0x210, 0x0c, None),
                          (0x126, 0x125, 0x212, 0, None),
                          (0x131, 0, 0x211, 0, (38, 0x51480))])
        snapshots = ROOT / 'build/rgn-scenes-v026'
        if not (snapshots / 'before.bin').exists():
            self.skipTest('saved original game checkpoints not available')
        expected = {'before': [0, 1, 0], 'open': [1, 1, 0],
                    'taken': [1, 1, 0], 'closed': [0, 1, 0]}
        for name, flags in expected.items():
            ds, observed = live_cache(snapshots / name, len(records))
            self.assertEqual(ds, 0x5b46)
            self.assertEqual(observed, flags)

    def test_optional_original_drawer_pixels_match_cache(self):
        snapshots = ROOT / 'build/rgn-scenes-v026'
        if not (snapshots / 'before.ppm').exists():
            self.skipTest('saved original framebuffer checkpoints not available')

        def drawer_pixels(name):
            data = (snapshots / (name + '.ppm')).read_bytes()
            header = b'P6\n640 480\n255\n'
            self.assertTrue(data.startswith(header))
            rgb = memoryview(data)[len(header):]
            self.assertEqual(len(rgb), 640 * 480 * 3)
            return b''.join(bytes(rgb[(y * 640 + 349)*3:
                                      (y * 640 + 402)*3])
                            for y in range(150, 226))

        before = drawer_pixels('before')
        opened = drawer_pixels('open')
        taken = drawer_pixels('taken')
        closed = drawer_pixels('closed')
        self.assertEqual(before, closed)
        self.assertEqual(opened, taken)
        pixel_changes = sum(before[i:i+3] != opened[i:i+3]
                            for i in range(0, len(before), 3))
        self.assertEqual(pixel_changes, 2751)

    def test_optional_original_drawer_picture_position(self):
        path = ROOT.parent / 'gateway-dos/GATE_102.PIC'
        if not path.exists():
            self.skipTest('original GATE_102.PIC not installed')
        image = path.read_bytes()
        offset, flags, width, height, _unused = struct.unpack_from(
            '<I4H', image, 16 * 12)
        self.assertEqual((flags, width, height), (0x4201, 48, 65))
        self.assertEqual(struct.unpack_from('<2H', image, offset), (90, 159))


if __name__ == '__main__':
    unittest.main()
