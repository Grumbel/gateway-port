#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Pin the resident DOS VFX mode-13h rasterizer translated by the C host.

Uses a private original 22-second RAM capture, if available. The capture is
never copied into the repository or encoded in generated test fixtures.
"""

from pathlib import Path
import hashlib
import struct
import unittest


ROOT = Path(__file__).resolve().parents[1]
SNAPSHOT = ROOT / 'build/intro-analysis/at-22000.bin'
MODE12_SNAPSHOT = ROOT / 'build/intro-analysis/at-1000.bin'
VFX_DS = 0x2a7a * 16
ORIGINAL_RTL = ROOT.parent / 'gateway-dos/GATE.RTL'
ORIGINAL_RTL_SHA256 = 'c187f5ee7be86482cff89e668d934dc961ac50e960df7baa98965910f1ea9566'


class OriginalRtlVfxRasterizer(unittest.TestCase):
    """Read the original RTL, not a missing or assumed private RAM snapshot."""

    @classmethod
    def setUpClass(cls):
        if not ORIGINAL_RTL.is_file():
            raise unittest.SkipTest('original GATE.RTL unavailable')
        cls.rtl = ORIGINAL_RTL.read_bytes()
        if hashlib.sha256(cls.rtl).hexdigest() != ORIGINAL_RTL_SHA256:
            raise unittest.SkipTest('GATE.RTL does not match the pinned executable')

    def test_mode13_original_row_copy_and_raster_branches(self):
        # GATE.RTL:184e8 sets bp-0a = row bytes, bp-08 = row count,
        # bp-1a = source row skip, bp-1c = VGA destination row skip.
        self.assertEqual(self.rtl[0x18565:0x18571],
                         bytes.fromhex('8b44328946e28b44308946f6'))
        self.assertEqual(self.rtl[0x185d2:0x185eb],
                         bytes.fromhex('8b44148946f22b46f68946e6268b451c8946f42b46f68946e4'))
        # Native linked 24a7:04c4 is the corresponding original RTL:189a0
        # handler. Mode zero is NOT a byte-by-byte read/modify/write loop:
        # SHR width, REP MOVSW, RCL width, REP MOVSB handles the odd byte.
        self.assertEqual(self.rtl[0x189b6:0x189ca],
                         bytes.fromhex('8b460c3d0000740c3d0300741d3d0100742deb40'))
        self.assertEqual(self.rtl[0x189ca:0x189de],
                         bytes.fromhex('8b4ef6d1e9f3a5d1d1f3a403f203fbff4ef875ec'))
        self.assertEqual(self.rtl[0x189e3:0x189e8],
                         bytes.fromhex('ac26300547'))  # XOR
        self.assertEqual(self.rtl[0x189f8:0x189fd],
                         bytes.fromhex('ac26200547'))  # AND
        self.assertEqual(self.rtl[0x18a0d:0x18a12],
                         bytes.fromhex('ac26080547'))  # OR

    def test_mode12_uses_vga_hardware_rasterizer_not_bios_pixels(self):
        # Original RTL:187da (resident 24a7:02fe) sets GC5=8 (read mode
        # 1, write mode 0), color-don't-care 0 and the full GC8 bit mask.
        self.assertEqual(self.rtl[0x187e9:0x187fd],
                         bytes.fromhex('b005bace03ee42b008eeb007bace03ee42b000ee'))
        # RTL:1885b shifts the raster operation left three bits and writes
        # Graphics Controller register 3, unlike our BIOS-pixel adapter.
        self.assertEqual(self.rtl[0x1885b:0x18869],
                         bytes.fromhex('b103d3e3b003bace03ee428ac3ee'))
        # The byte-aligned source path consumes one source byte and
        # touches one VGA byte (eight pixels) per iteration.
        self.assertEqual(self.rtl[0x18870:0x18881],
                         bytes.fromhex('81f900ff753e578b4ef6ac26200547e2f9'))
        # Advance by the source stride for each plane, cycle sequencer
        # masks 1/2/4/8 and advance the destination only after plane 8.
        self.assertEqual(self.rtl[0x18881:0x1888b],
                         bytes.fromhex('5f0376e6807ef0017411'))
        self.assertEqual(self.rtl[0x1888b:0x1889c],
                         bytes.fromhex('8b56ecd066ee3856ee76118b56ea8856ee'))
        # RTL:188b4 handles unaligned origins AND partial source bytes.
        # The special one-source-byte row selects CH for both leading and
        # trailing masks; otherwise CBW sign-extends CH before SHL AX,CL.
        self.assertEqual(self.rtl[0x188b4:0x188d6], bytes.fromhex(
            'bbff008ac598837ef60175068add8ae532c0d3e0d3e3'
            '8866e08846de887ee28b5ef6'))
        # The actual data loop uses a sliding LODSW; DEC SI; ROL AX,CL
        # source window, then CPU AND against VGA latches and GC8 masks.
        self.assertEqual(self.rtl[0x188e4:0x188f6], bytes.fromhex(
            'ad4e84c975054b7509eb21d3c0262025474b'))
        self.assertEqual(self.rtl[0x18909:0x18920], bytes.fromhex(
            'ad4ed3c04b75f58bd8b008bace03ee428a46e0ee26201d'))
        # The original resets fixed GC/SEQ defaults rather than restoring
        # a caller-saved register snapshot.
        self.assertEqual(self.rtl[0x1895c:0x18970],
                         bytes.fromhex('b005bace03ee42b000eeb007bace03ee42b00fee'))
        self.assertEqual(self.rtl[0x1897a:0x1898e],
                         bytes.fromhex('b002bac403ee42b00feeb003bace03ee42b000ee'))

    def test_original_mode12_single_pixel_uses_gc_write_mode_two(self):
        # Original resident 25d7:00d6 maps to RTL:19a69. The coordinate
        # mapper RTL:18e28 computes y*80 + x/8 and CL = 7-(x&7).
        self.assertEqual(self.rtl[0x18e2b:0x18e39],
                         bytes.fromhex('ba5000f7e25ad1ebd1ebd1eb03d8'))
        self.assertEqual(self.rtl[0x18e40:0x18e48],
                         bytes.fromhex('80e10780f107b401'))
        # GC8 selects a single pixel; GC5=02h selects write mode 2,
        # which expands AL's color nibble into all four enabled planes.
        self.assertEqual(self.rtl[0x19a83:0x19a8f],
                         bytes.fromhex('d2e4bace03b008efb80502ef'))
        # The relocated SI structure supplies the page byte offset (+0a)
        # and raster operation (+16), both from the resident draw state.
        self.assertEqual(self.rtl[0x19a8f:0x19a95],
                         bytes.fromhex('be0000035c0a'))
        # GC3 is the original raster operation <<3. The VGA read loads
        # all latches, and the write sends AL=color without BIOS INT10h.
        self.assertEqual(self.rtl[0x19a95:0x19aa8],
                         bytes.fromhex('8a6416b103d2e4b003ef268a078a4606268807'))
        self.assertEqual(self.rtl[0x19aa8:0x19ab4],
                         bytes.fromhex('b808ffefb80500efb80300ef'))

    def test_original_mode13_horizontal_copy_uses_rep_stosb(self):
        # RTL:19f2d's line router switches to RTL:1a535's 320-byte pitch
        # backend. Horizontal COPY takes the inclusive REP STOSB branch.
        self.assertEqual(self.rtl[0x1a535:0x1a54b],
                         bytes.fromhex('837ede007405ea00000000be40018b4e082b4e0c745e'))
        self.assertEqual(self.rtl[0x1a5d1:0x1a5e6],
                         bytes.fromhex('518b460a8b5e0c9a000000008bfb59418a46dcf3aa'))
        # RTL:1a6ee's solid rectangle loops over scanlines, issuing a
        # four-coordinate horizontal line call for each row.
        self.assertEqual(self.rtl[0x1a73a:0x1a755],
                         bytes.fromhex('c74428ffff8b7e0cff760e57ff760a579a00000000473b7e0876ed'))

    def test_original_mode13_vertical_and_bresenham_lines(self):
        # RTL:1a5a9 chooses the topmost Y, counts the last pixel and
        # advances by 320 bytes after each STOSB (DI was incremented by 1).
        self.assertEqual(self.rtl[0x1a5b1:0x1a5ba],
                         bytes.fromhex('2bc87d04f7d98bc341'))
        self.assertEqual(self.rtl[0x1a5c4:0x1a5cf],
                         bytes.fromhex('8bfb4e8a46dcaa03fee2fb'))
        # RTL:1a580 initializes Bresenham: twice the minor axis, then
        # 2*minor-major and 2*minor-2*major; the loops branch at sign 0.
        self.assertEqual(self.rtl[0x1a580:0x1a58e],
                         bytes.fromhex('d1e3895ee82bd98bf32bd9895ee6'))
        self.assertEqual(self.rtl[0x1a5e8:0x1a617], bytes.fromhex(
            '8a46dcaa0bf679070376e8e2f6eb200376e603fbe2edeb17'
            '8a46dcaa03fb0bf679080376e84fe2f3eb050376e6e2ec'))

    def test_original_planar_vertical_uses_gc_set_reset_and_latches(self):
        # RTL:1a39c programs GC0 = color, GC1 = 0f, GC3 = op<<3.
        self.assertEqual(self.rtl[0x1a39c:0x1a3b3], bytes.fromhex(
            'bace038a66dc32c0efb8010fef8a66deb103d2e4b003ef'))
        # RTL:1a425 normalizes Y, and 1a43f selects the pixel in GC8.
        # CPU OR ES:[BX],AL reads the VGA latches, then writes its set/reset
        # color at each 80-byte row stride; the end pixel is included.
        self.assertEqual(self.rtl[0x1a43f:0x1a44c],
                         bytes.fromhex('d2e4b008ef5926080703dee2f9'))
        # Original fixed-register cleanup, not a restoration of saved VGA
        # controller values: GC0=0, GC1=0, GC3=0, GC8=ff.
        self.assertEqual(self.rtl[0x1a517:0x1a523],
                         bytes.fromhex('33c0ef40efb003efb808ffef'))

    def test_original_planar_horizontal_masks_and_movsb(self):
        # RTL:1a44f derives the starting mask from the first pixel's bit
        # and the final mask from the inclusive last pixel's bit.
        self.assertEqual(self.rtl[0x1a471:0x1a481],
                         bytes.fromhex('8b4e0880e10780f107b2ffd2e22256d8'))
        # RTL:1a49c selects a shared-byte intersection for CX=0, or
        # masked first-byte MOVSB, GC8=full-byte REP MOVSB, and masked
        # last-byte MOVSB. Source/dest are both the VGA aperture.
        self.assertEqual(self.rtl[0x1a49c:0x1a4b7], bytes.fromhex(
            '0aff780d0bc9750422dfeb0b8ae7efa449'
            '8a66d8eff3a48ae3efa4'))


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

    def test_original_graphics_font_background_services(self):
        # RTL:edff invokes a resident fill-state setter and rectangle
        # rasterizer before placing any ink pixels, but only when the
        # original text background color is nonnegative. The RAM snapshot
        # proves these are separate FAR services, not font bitmap routines.
        linked = 0x1c4d0 + 0x0511 - 0xebf7
        for original, offset, segment in ((0xeeb6, 0x0002, 0x25a3),
                                          (0xeede, 0x0006, 0x2698)):
            call = linked + original
            self.assertEqual(self.ram[call], 0x9a)
            self.assertEqual((self.word(call + 1), self.word(call + 3)),
                             (offset, segment))
        self.assertEqual(self.ram[0x25a32], 0x55)
        self.assertEqual(self.ram[0x26986], 0x55)

    def test_original_palette_driver_relocated_globals(self):
        # Original RTL:f61b is linked at 0cdc:04d2 in the verified bank.
        # Its live operands use DS offsets, not raw RTL file placeholders.
        self.assertEqual(self.ram[0x1d292], 0x55)  # far-call prologue
        for site, offset in ((0x1d29c, 0x0fce),
                             (0x1d2aa, 0x5516),
                             (0x1d0f4, 0x5514),
                             (0x1d40f, 0x4daa),
                             (0x1d36d, 0x132a),
                             (0x1d377, 0x1334)):
            self.assertEqual(self.word(site), offset)
        self.assertEqual(self.word(VFX_DS + 0x0fce), 3)
        self.assertEqual(self.word(VFX_DS + 0x5514), 0x03da)
        self.assertEqual(self.word(VFX_DS + 0x5516), 5)
        self.assertEqual(self.word(VFX_DS + 0x0fd8), 768)
        self.assertEqual(self.word(VFX_DS + 0x4daa), 0x2a70)


if __name__ == '__main__':
    unittest.main()
