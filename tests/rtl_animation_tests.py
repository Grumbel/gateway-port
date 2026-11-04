#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Synthetic and optional original-image tests for RTL animation services."""

from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from inspect_rtl_animation import ROOM_CALLS, inspect_animation_calls


class AnimationServiceTests(unittest.TestCase):
    def setUp(self):
        self.ovl = bytearray(0x51570)
        self.exe = b'MZ' + bytes(0x800)
        self.rtl = bytearray(0x4000)
        self.ram = bytearray(1 << 20)
        self.origin = 0x400
        for index, (_, call, segment, offset, _) in enumerate(ROOM_CALLS):
            struct.pack_into('<BHH', self.ovl, call, 0x9a, offset, segment)
            # Unique, fully synthetic 32-byte entries at a common source base.
            code = bytes(((value * 19 + index * 31 + 7) % 256)
                         for value in range(32))
            source_offset = self.origin + offset
            guest_linear = (0x1000 + segment) * 16 + offset
            self.rtl[source_offset:source_offset + len(code)] = code
            self.ram[guest_linear:guest_linear + len(code)] = code

    def resolve(self):
        return inspect_animation_calls(self.ovl, self.exe, self.rtl, self.ram)

    def test_four_synthetic_far_calls_have_common_rtl_origin(self):
        rows = self.resolve()
        self.assertEqual(len(rows), 4)
        self.assertEqual([row['original_offset'] for row in rows],
                         [self.origin + item[3] for item in ROOM_CALLS])
        self.assertTrue(all(row['matched_bytes'] == 16 for row in rows))
        self.assertTrue(all(row['segment_support'] == 4 for row in rows))

    def test_changed_overlay_call_is_rejected(self):
        self.ovl[ROOM_CALLS[0][1]] = 0x90
        with self.assertRaisesRegex(ValueError, 'linked far CALL changed'):
            self.resolve()

    def test_unresolved_guest_entry_is_rejected(self):
        _, _, segment, offset, _ = ROOM_CALLS[2]
        guest_linear = (0x1000 + segment) * 16 + offset
        self.ram[guest_linear:guest_linear + 128] = b'\x00' * 128
        with self.assertRaisesRegex(ValueError, 'could not uniquely match'):
            self.resolve()

    def test_optional_original_rtl_animation_services(self):
        dos = ROOT.parent / 'gateway-dos'
        snapshot = ROOT / 'build/rgn-scenes-v026/before.bin'
        files = [dos / 'GATEMAIN.OVL', dos / 'GATE.EXE',
                 dos / 'GATE.RTL', snapshot]
        if not all(file.exists() for file in files):
            self.skipTest('original DOS files and captured guest RAM unavailable')
        ovl, exe, rtl, ram = (file.read_bytes() for file in files)
        rows = inspect_animation_calls(ovl, exe, rtl, ram)
        self.assertEqual([row['original_offset'] for row in rows],
                         [0x1328d, 0x133df, 0x1342d, 0x136b1])

        # Read-only checks of original machine-code invariants: five active
        # sequence slots, 20 frame records, 0x88-byte frame stride, and a
        # global active-count reset. The RTL service's DS register is not
        # established by the game's DS:5b46 checkpoint alone.
        self.assertEqual(rtl[0x13295:0x132a2], bytes.fromhex(
            '833e1200057406837e08147e09'))
        self.assertIn(bytes.fromhex('81c68800'), rtl[0x1328d:0x13387])
        self.assertEqual(rtl[0x1373a:0x13740],
                         bytes.fromhex('c70612000000'))
        self.assertIn(bytes.fromhex('b03226f66707'),
                      rtl[0x134a5:0x136b1])


if __name__ == '__main__':
    unittest.main()
