#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Pin the resident DOS VFX mode-13h rasterizer translated by the C host.

Uses a private original 22-second RAM capture, if available. The capture is
never copied into the repository or encoded in generated test fixtures.
"""

from pathlib import Path
import struct
import unittest


ROOT = Path(__file__).resolve().parents[1]
SNAPSHOT = ROOT / 'build/intro-analysis/at-22000.bin'
MODE12_SNAPSHOT = ROOT / 'build/intro-analysis/at-1000.bin'
VFX_DS = 0x2a7a * 16


class NativeVfxProvenance(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not SNAPSHOT.is_file():
            raise unittest.SkipTest('private linked VFX guest RAM unavailable')
        cls.ram = SNAPSHOT.read_bytes()
        if len(cls.ram) != 0x100000:
            raise unittest.SkipTest('not a 1MiB original RAM snapshot')

    def word(self, linear):
        return struct.unpack_from('<H', self.ram, linear)[0]

    def test_vfx_rasterizer_and_mode13h_dispatch(self):
        # 24a7:000c (resident, *not* a relative GATE.RTL file offset).
        self.assertEqual(self.ram[0x24a7c:0x24a7f], b'\x55\x8b\xec')
        self.assertEqual(self.ram[0x24b7f:0x24b83], b'\x26\xff\x6d\x2a')

        descriptor = VFX_DS + 0x27ee + 3 * 0x3e
        self.assertEqual(self.word(VFX_DS + 0x15cc), 3)
        self.assertEqual(self.word(VFX_DS + 0x15d2), 1)
        self.assertEqual(self.ram[descriptor + 0x16:descriptor + 0x18],
                         b'\x13\x08')
        self.assertEqual([self.word(descriptor + p)
                          for p in (0x18, 0x1a, 0x1c)], [320, 200, 320])
        self.assertEqual([self.word(descriptor + p)
                          for p in (0x26, 0x28, 0x2a, 0x2c)],
                         [0x0053, 0x252c, 0x04c4, 0x24a7])

    def test_original_mode13h_coordinate_mapping(self):
        # 252c:0053: DX-independent, ES=A000; DI=320*y+x (16-bit).
        self.assertEqual(self.ram[0x25313:0x2531d],
                         bytes.fromhex('86e003d8d1e8d1e803d8'))
        self.assertEqual(self.ram[0x2531e:0x25324],
                         bytes.fromhex('ba00a08ec25a'))

    def test_original_four_pixel_operations(self):
        # 24a7:04c4: byte-per-pixel renderer, with mode 0 copy, mode 3 XOR,
        # mode 1 AND, and all remaining modes OR (ALU opcodes below).
        self.assertEqual(self.ram[0x24f34:0x24f3e],
                         bytes.fromhex('8b5e0a8b460826ff5d26'))
        self.assertEqual(self.ram[0x24f4a:0x24f5e],
                         bytes.fromhex('8b460c3d0000740c3d0300741d3d0100742deb40'))
        self.assertEqual(self.ram[0x24f63:0x24f68],
                         bytes.fromhex('f3a5d1d1f3'))
        self.assertEqual(self.ram[0x24f77:0x24f7c],
                         bytes.fromhex('ac26300547'))
        self.assertEqual(self.ram[0x24f8c:0x24f91],
                         bytes.fromhex('ac26200547'))
        self.assertEqual(self.ram[0x24fa1:0x24fa6],
                         bytes.fromhex('ac26080547'))
        self.assertEqual(self.ram[0x24fc0:0x24fc3],
                         bytes.fromhex('ca0c00'))

    def test_original_image_object_layout(self):
        # 244c:0025 accepts CA00 image records, reads bpp at +13, stride
        # at +14, and pixel FAR pointer at +18. VFX reads clip extents.
        self.assertEqual(self.ram[0x244f7:0x244fc],
                         bytes.fromhex('26813d00ca'))
        self.assertEqual(self.ram[0x24530:0x24534],
                         bytes.fromhex('268a4513'))
        self.assertEqual(self.ram[0x2453e:0x24546],
                         bytes.fromhex('268a451226f76514'))
        self.assertEqual(self.ram[0x2455b:0x24562],
                         bytes.fromhex('2603451883d200'))
        self.assertEqual(self.ram[0x24af9:0x24b05],
                         bytes.fromhex('8b44328946e28b44308946f6'))

    def test_original_mode12_four_plane_rasterizer(self):
        # The early title is shown in VGA mode 12h, before later mode 13h.
        # Original 2409:00e8 returns descriptor #2 for 640x480, 1 bit,
        # four independently addressable video planes and a pitch of 80.
        if not MODE12_SNAPSHOT.is_file():
            self.skipTest('private original 640x480 guest RAM unavailable')
        ram = MODE12_SNAPSHOT.read_bytes()
        self.assertEqual(len(ram), 0x100000)
        self.assertEqual(struct.unpack_from('<H', ram, VFX_DS + 0x15cc)[0], 2)
        descriptor = VFX_DS + 0x27ee + 2 * 0x3e
        self.assertEqual(ram[descriptor + 0x16:descriptor + 0x18], b'\x12\x01')
        self.assertEqual([struct.unpack_from('<H', ram, descriptor + off)[0]
                          for off in (0x18, 0x1a, 0x1c)], [640, 480, 80])
        self.assertEqual(ram[descriptor + 0x1e], 4)
        self.assertEqual([struct.unpack_from('<H', ram, descriptor + off)[0]
                          for off in (0x26, 0x28, 0x2a, 0x2c)],
                         [0x0032, 0x252c, 0x02fe, 0x24a7])
        # 24a7:02fe: exclusive mode-12h VGA map-mask/bit-mask handler.
        self.assertEqual(ram[0x24d6e:0x24d7d],
                         bytes.fromhex('8b460a3407fec024078a66e28946e2'))
        self.assertEqual(ram[0x24d7d:0x24d91],
                         bytes.fromhex('b005bace03ee42b008eeb007bace03ee42b000ee'))
        # Original 245d:0006 constructor stores plane count * height
        # at image+34; 2479:0002 uses this total scanline count directly.
        self.assertEqual(ram[0x24639:0x24647],
                         bytes.fromhex('8a441e26884512f7660626894534'))
        self.assertEqual(ram[0x247d6:0x247e6],
                         bytes.fromhex('268b5d34895ef633db268a5d12895ef8'))

    def test_original_rtl_glyph_pixel_call_and_video_dispatch(self):
        # Original RTL:ef83 in the *relocated* 0c4d text bank links to the
        # resident graphics service 25d7:0004, not a function in GATE.RTL.
        # Its table selects the exact mode-12h and mode-13h pixel routines.
        call = 0x1c4d0 + 0x0511 + (0xef83 - 0xebf7)
        self.assertEqual(self.ram[call], 0x9a)  # CALL FAR
        self.assertEqual((self.word(call + 1), self.word(call + 3)),
                         (0x0004, 0x25d7))
        self.assertEqual(self.ram[0x25d74], 0x55)  # dispatcher prologue
        self.assertEqual(self.word(VFX_DS + 0x15cc), 3)
        for mode, handler in ((2, 0x00d6), (3, 0x0133)):
            entry = VFX_DS + 0x2a8f + 20 * mode
            self.assertEqual((self.word(entry + 4), self.word(entry + 6)),
                             (handler, 0x25d7))
        # Real 22s clipping is enabled and limits the 320x200 aperture.
        self.assertEqual(self.word(VFX_DS + 0x29de + 0x18), 1)
        self.assertEqual([self.word(VFX_DS + 0x29de + off)
                          for off in (0x1a, 0x1c, 0x1e, 0x20)],
                         [0, 0, 319, 199])

    def test_original_linked_text_pitch_sources(self):
        # These segment selectors in the linked RTL are patched from zero
        # placeholders in the file. They point into the current VGA mode
        # descriptor. The scalars are read by real source-C text helpers.
        for slot in (0x4d82, 0x4d86, 0x4d8a, 0x4d90):
            self.assertEqual(self.word(VFX_DS + slot), 0x2a70)
        mode = 0x2a70 * 16
        self.assertEqual([self.word(mode + off)
                          for off in (0x34, 0x3a, 0x3c, 0x3e)],
                         [11, 24, 8, 319])


if __name__ == '__main__':
    unittest.main()
