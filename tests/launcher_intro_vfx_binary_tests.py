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


if __name__ == '__main__':
    unittest.main()
