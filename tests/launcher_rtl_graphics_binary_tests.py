#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Validate original RTL intro source identities and live linked far calls.

Original binaries and optional captured RAM are inputs only, never emitted.
The tests do not claim entry-state differential equivalence of the C code.
"""

import hashlib
from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from rank_engine_services import resolve_linked_origins  # noqa: E402

RTL_HASH = 'c187f5ee7be86482cff89e668d934dc961ac50e960df7baa98965910f1ea9566'
EXE_HASH = '767ccbbacbd6b304a5bab072894a45ca426a2196ae369b2f11d02ef92bb7134b'

# Linked addresses are from GATE.EXE and are NOT file offset identities.
GRAPHICS_TARGETS = {
    (0x0f10, 0x1014): 0x12b19,
    (0x0f10, 0x0d8e): 0x12893,
    (0x0f10, 0x1680): 0x13185,
    (0x0f10, 0x112e): 0x12c33,
    (0x0f10, 0x13d2): 0x12ed7,
    (0x0f10, 0x16c6): 0x131cb,
    (0x0f10, 0x0c6e): 0x12773,
    (0x0f10, 0x0d1e): 0x12823,
    (0x0f10, 0x15a6): 0x130ab,
    (0x0f10, 0x1466): 0x12f6b,
    (0x0cdc, 0x04d2): 0xf61b,
    (0x0cdc, 0x07d8): 0xf921,
    (0x0cdc, 0x0004): 0xf14d,
    (0x0cdc, 0x0aac): 0xfbf5,
    (0x0cdc, 0x0aee): 0xfc37,
    (0x0cdc, 0x0e50): 0xff99,
}


class OriginalIntroGraphicsEvidence(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        for key, name, expected in [('rtl', 'GATE.RTL', RTL_HASH),
                                     ('exe', 'GATE.EXE', EXE_HASH)]:
            path = ROOT.parent / 'gateway-dos' / name
            if not path.is_file():
                raise unittest.SkipTest('original DOS images unavailable')
            content = path.read_bytes()
            if hashlib.sha256(content).hexdigest() != expected:
                raise unittest.SkipTest(f'{name} does not match pinned original')
            setattr(cls, key, content)

    def test_bounded_graphics_functions(self):
        for start, end in ((0xf473, 0xf49a),
                           (0xf49b, 0xf4de),
                           (0xf61b, 0xf826),
                           (0x12b19, 0x12b66),
                           (0x12c33, 0x12d66),
                           (0x1266f, 0x12771),
                           (0x12ed7, 0x12f2a),
                           (0x12f2b, 0x12f6a),
                           (0x13115, 0x13184),
                           (0x13191, 0x131c8),
                           (0x12d67, 0x12dda),
                           (0x130ab, 0x13114),
                           (0x131f7, 0x1323e),
                           (0x12205, 0x1230e),
                           (0x12383, 0x123d3),
                           (0x12773, 0x12821),
                           (0x12823, 0x12891),
                           (0x12893, 0x12995),
                           (0x13185, 0x13190),
                           (0x131cb, 0x131f5)):
            with self.subTest(original_rtl_offset=start):
                # Each has the original BP-based C prologue and FAR return.
                self.assertEqual(self.rtl[start], 0x55)
                self.assertEqual(self.rtl[start + 1], 0x8b)
                self.assertEqual(self.rtl[start + 2], 0xec)
                self.assertEqual(self.rtl[end], 0xcb)

    def test_original_intro_video_reset_wrapper(self):
        # RTL:f921 is a distinct 21-byte FAR-returning routine linked as
        # 0cdc:07d8, not part of the following f937 cursor-mode setter.
        self.assertEqual(self.rtl[0xf921], 0x2b)  # SUB AX, AX
        self.assertEqual(self.rtl[0xf923], 0x50)  # PUSH AX twice
        self.assertEqual(self.rtl[0xf924], 0x50)
        self.assertEqual(self.rtl[0xf925], 0x9a)  # FAR call
        self.assertEqual(int.from_bytes(self.rtl[0xf92c:0xf92e], 'little'),
                         0x030e)  # original cursor block-mode WORD
        self.assertEqual(self.rtl[0xf931], 0x0e)  # PUSH CS
        self.assertEqual(self.rtl[0xf932], 0xe8)  # near-call f8eb
        displacement = int.from_bytes(self.rtl[0xf933:0xf935],
                                      'little', signed=True)
        self.assertEqual(0xf935 + displacement, 0xf8eb)
        self.assertEqual(self.rtl[0xf935], 0xcb)  # RETF

        snapshot = ROOT / 'build/intro-analysis/at-22000.bin'
        if not snapshot.is_file():
            self.skipTest('private guest RAM snapshot unavailable')
        ram = snapshot.read_bytes()
        base = (0x0cdc + 0x1000) * 16 + 4 - 0xf14d
        call = ram[base + 0xf925:base + 0xf92a]
        self.assertEqual(call[0], 0x9a)
        self.assertEqual((int.from_bytes(call[1:3], 'little'),
                          int.from_bytes(call[3:5], 'little')),
                         (0x000a, 0x2488))
        self.assertEqual(int.from_bytes(ram[base + 0xf92c:base + 0xf92e],
                                        'little'), 0x12dc)

    def test_original_resident_video_clear_dispatch_and_vga_handlers(self):
        snapshot = ROOT / 'build/intro-analysis/at-22000.bin'
        if not snapshot.is_file():
            self.skipTest('private guest RAM snapshot unavailable')
        ram = snapshot.read_bytes()
        ds = 0x2a7a * 16

        # The captured FAR body starts at 2488:000a. It reads the active
        # selector from 2409:00b9 and retrieves its 3eh-byte descriptor
        # from 2409:00e8; mode-to-callback setup is 2488:00a2.
        self.assertEqual(ram[0x2488a:0x2488d], bytes.fromhex('558bec'))
        self.assertEqual(ram[0x248ab:0x248b0],
                         bytes.fromhex('9ab9000924'))
        self.assertEqual(ram[0x248bd:0x248c2],
                         bytes.fromhex('9ae8000924'))
        self.assertEqual(ram[0x24912:0x24915],
                         bytes.fromhex('ff6c3a'))  # JMP FAR [SI+3ah]
        self.assertEqual(ram[0x248cf:0x248d5],
                         bytes.fromhex('837efc017512'))
        self.assertEqual(ram[0x248d5:0x248db],
                         bytes.fromhex('55b40fcd105d'))  # INT 10h/AH=0fh
        self.assertEqual(ram[0x248db:0x248e5],
                         bytes.fromhex('3a44167407c746fef9ff'))  # -7
        self.assertEqual(ram[0x24107:0x24111],
                         bytes.fromhex('55b40fcd105d263a4516'))
        self.assertEqual(ram[0x24113:0x2411c],
                         bytes.fromhex('268a451655b400cd10'))  # BIOS set

        self.assertEqual(ram[ds + 0x15cc:ds + 0x15ce], b'\x03\x00')
        self.assertEqual(ram[ds + 0x27ed], 1)  # initialized callback table
        callbacks = (0x011a, 0x0169, 0x0169, 0x01d7,
                     0x0169, 0x0169, 0x0169, 0x0169)
        for mode, callback in enumerate(callbacks):
            entry = ds + 0x27ee + mode * 0x3e
            with self.subTest(mode=mode):
                self.assertEqual(ram[entry], mode)
                self.assertEqual(int.from_bytes(ram[entry + 0x3a:
                                                     entry + 0x3c], 'little'),
                                 callback)
                self.assertEqual(int.from_bytes(ram[entry + 0x3c:
                                                     entry + 0x3e], 'little'),
                                 0x2488)

        for mode, height, pitch, planes, segment, stride in (
                (0, 200, 80, 1, 0xb800, 0),
                (1, 350, 80, 4, 0xa000, 0x8000),
                (2, 480, 80, 4, 0xa000, 0),
                (3, 200, 320, 1, 0xa000, 0),
                (7, 600, 100, 4, 0xa000, 0)):
            entry = ds + 0x27ee + mode * 0x3e
            with self.subTest(descriptor=mode):
                self.assertEqual(int.from_bytes(ram[entry + 0x1a:
                                                     entry + 0x1c], 'little'),
                                 height)
                self.assertEqual(int.from_bytes(ram[entry + 0x1c:
                                                     entry + 0x1e], 'little'),
                                 pitch)
                self.assertEqual(ram[entry + 0x1e], planes)
                self.assertEqual(int.from_bytes(ram[entry + 0x20:
                                                     entry + 0x22], 'little'),
                                 segment)
                self.assertEqual(int.from_bytes(ram[entry + 0x22:
                                                     entry + 0x24], 'little'),
                                 stride)

        # Mode 1/2/4..7: sequencer all planes, GC mode2 seed + mode1
        # latched-byte replication for height*pitch bytes.
        self.assertEqual(ram[0x249e9:0x249f3],
                         bytes.fromhex('b002bac403ee42b00fee'))
        self.assertEqual(ram[0x24a22:0x24a31],
                         bytes.fromhex('8b46f48b5ef8f7e38bc8268a05f3aa'))
        # Mode 3: 16-bit REP STOSW over (height * pitch) / 2 words;
        # AH receives AL, giving identical bytes for the whole VGA page.
        self.assertEqual(ram[0x24a57:0x24a6a],
                         bytes.fromhex('8b46f48b5ef6f7e3d1e88bc88b46088ae0f3ab'))

    def test_original_video_mode_switch_and_resident_services(self):
        # The large f14d C routine has multiple FAR returns, and calls
        # f49b through PUSH CS/near CALL after successful BIOS setup.
        self.assertEqual(self.rtl[0xf14d:0xf150], b'\x55\x8b\xec')
        self.assertEqual(self.rtl[0xf16c], 0xcb)
        self.assertEqual(self.rtl[0xf389], 0xcb)
        self.assertEqual(self.rtl[0xf25a], 0x0e)
        self.assertEqual(self.rtl[0xf25b], 0xe8)
        delta = int.from_bytes(self.rtl[0xf25c:0xf25e], 'little', signed=True)
        self.assertEqual(0xf25e + delta, 0xf49b)
        snapshot = ROOT / 'build/intro-analysis/at-22000.bin'
        if not snapshot.is_file():
            self.skipTest('private guest RAM snapshot unavailable')
        ram = snapshot.read_bytes()
        # Original f14d is relocated to 0cdc:0004, loaded at 1cdc4;
        # hardware targets are observed, not guessed from placeholders.
        base = (0x0cdc + 0x1000) * 16 + 4 - 0xf14d
        calls = ((0xf172, (0x000a, 0x242a)),
                 (0xf19d, (0x0004, 0x241d)),
                 (0xf238, (0x0008, 0x2409)),
                 (0xf248, (0x003e, 0x2409)),
                 (0xf374, (0x003a, 0x25b7)),
                 (0xf37d, (0x0008, 0x25b7)))
        for site, target in calls:
            with self.subTest(callsite=site):
                linked = ram[site + base:site + base + 5]
                self.assertEqual(linked[0], 0x9a)
                self.assertEqual((int.from_bytes(linked[1:3], 'little'),
                                  int.from_bytes(linked[3:5], 'little')),
                                 target)
        # These are relocated DS operands for the same globals used by
        # the native mode host, not locations from a guessed C struct.
        for site, expected in ((0xf156, 0x0fce), (0xf17d, 0x5516),
                               (0xf233, 0x5514), (0xf265, 0x4d92),
                               (0xf271, 0x4d94), (0xf27d, 0x4d96),
                               (0xf289, 0x4d98), (0xf2aa, 0x4d9a),
                               (0xf2b7, 0x4d9c), (0xf2bf, 0x4d9e)):
            with self.subTest(ds_operand=site):
                self.assertEqual(int.from_bytes(
                    ram[site + base:site + base + 2], 'little'), expected)
        # The two resident drawing calls store the signed clip rectangle
        # and switch its enable WORD in the same state read by the font
        # pixel dispatcher. Validate their real DOS bodies as well.
        self.assertEqual(ram[0x25b78:0x25b7b], b'\x55\x8b\xec')
        self.assertEqual(ram[0x25baa:0x25bad], b'\x55\x8b\xec')
        for site, offset in ((0x25b95, 0x18), (0x25bbf, 0x1a),
                             (0x25bc5, 0x1c), (0x25bcb, 0x1e),
                             (0x25bd1, 0x20)):
            with self.subTest(resident_clip=site):
                self.assertEqual(ram[site:site + 3],
                                 bytes((0x89, 0x44, offset)))
        # Resident 2409:0008 selects the live renderer's mode word; its
        # paired 2409:003e initializer stores the enabled/BIOS state.
        self.assertEqual(ram[0x240b9:0x240bc], b'\xa3\xcc\x15')
        self.assertEqual(ram[0x240fe:0x24101], b'\xa3\xce\x15')
        self.assertEqual(ram[0x24101:0x24107],
                         bytes.fromhex('c706d0150000'))

    def test_original_color_and_rectangle_wrappers(self):
        # Source-C endpoints in the original unrelocated RTL image:
        # ink fb f5, fill-pattern fc23, rectangle fc37, then next fc81.
        for start, end in ((0xfbf5, 0xfc22), (0xfc23, 0xfc36),
                           (0xfc37, 0xfc80)):
            self.assertEqual(self.rtl[start:start + 3], b'\x55\x8b\xec')
            self.assertEqual(self.rtl[end], 0xcb)
        self.assertEqual(self.rtl[0xfc2c:0xfc32],
                         bytes.fromhex('3d01001bc040'))
        self.assertEqual(self.rtl[0xfc3a:0xfc3e],
                         bytes.fromhex('f6460680'))
        self.assertEqual(self.rtl[0xfc60:0xfc6b],
                         bytes.fromhex('837e0aff7d05c7460affff'))

        snapshot = ROOT / 'build/intro-analysis/at-22000.bin'
        if not snapshot.is_file():
            self.skipTest('private guest RAM snapshot unavailable')
        ram = snapshot.read_bytes()
        # The linker places fb f5 at 0cdc:0aac (1d86c physical). Every
        # operand below was patched from the RTL's zero placeholders.
        base = (0x0cdc + 0x1000) * 16 + 0x0aac - 0xfbf5
        for site, offset in ((0xfbfc, 0x5512), (0xfc06, 0x4da8),
                             (0xfc0b, 0x0054), (0xfc14, 0x5512),
                             (0xfc2a, 0x12de), (0xfc33, 0x12e0),
                             (0xfc46, 0x5512), (0xfc51, 0x12de),
                             (0xfc55, 0x5512), (0xfc59, 0x12e0)):
            with self.subTest(ds_operand=site):
                self.assertEqual(int.from_bytes(ram[base + site:
                                                     base + site + 2],
                                                'little'), offset)
        for site, target in ((0xfc17, (0x011e, 0x25a3)),
                             (0xfc5b, (0x0002, 0x25a3)),
                             (0xfc7a, (0x0006, 0x2698))):
            with self.subTest(callsite=site):
                call = ram[base + site:base + site + 5]
                self.assertEqual(call[0], 0x9a)
                self.assertEqual((int.from_bytes(call[1:3], 'little'),
                                  int.from_bytes(call[3:5], 'little')),
                                 target)
        # The 25a3:011e resident color writer actually stores the ink
        # at DS:29de+0c; the fill setter accepts patterns 0..11 and
        # stores its state at +22/+24/+26.
        self.assertEqual(ram[0x25b60:0x25b66],
                         bytes.fromhex('8b460689440c'))
        for site, field, opcode in ((0x25a51, 0x22, 0x44),
                                    (0x25a57, 0x24, 0x44),
                                    (0x25a68, 0x26, 0x5c)):
            self.assertEqual(ram[site:site + 3], bytes((0x89, opcode, field)))
        # At the resident 25d7:0004 dispatcher, the linked handler table
        # DS:2a8f has 20-byte mode records. Both VGA mode 1 (640x350)
        # and mode 2 (640x480) select the SAME 25d7:00d6 planar callback.
        ds = (0x2a7a << 4)
        for mode in (1, 2):
            entry = ds + 0x2a8f + 20 * mode + 4
            self.assertEqual(ram[entry:entry + 4],
                             bytes.fromhex('d600d725'))
        self.assertEqual(int.from_bytes(ram[ds + 0x1302 + 2:
                                              ds + 0x1302 + 4], 'little'),
                         349)

    def test_original_caption_frame_and_line_services(self):
        # 0cdc:0e50 is the complete original RTL ff99..1029f frame
        # painter. Three style bits select its outline, inset bevel and
        # outset shadow; all endpoints are sent through the resident
        # 2621:000e (move) and 2621:003e (line-to) FAR entries.
        self.assertEqual(self.rtl[0xff99:0xff9c], b'\x55\x8b\xec')
        self.assertEqual(self.rtl[0x1029f], 0xcb)
        for site, pattern in ((0xffbf, 'f6460e02'),
                              (0x10036, 'f6460e04'),
                              (0x10187, 'f6460e08')):
            self.assertEqual(self.rtl[site:site + 4],
                             bytes.fromhex(pattern))
        snapshot = ROOT / 'build/intro-analysis/at-22000.bin'
        if not snapshot.is_file():
            self.skipTest('private guest RAM snapshot unavailable')
        ram = snapshot.read_bytes()
        base = (0x0cdc + 0x1000) * 16 + 0x0e50 - 0xff99
        for site, expected in ((0xffa0, 0x0fce),
                               (0xffa7, 0x4da6),
                               (0xffb1, 0x4da0),
                               (0x10041, 0x0fce),
                               (0x10073, 0x4da4),
                               (0x100a2, 0x4da6),
                               (0x100f3, 0x0fce),
                               (0x10107, 0x4da2),
                               (0x1013c, 0x0fce),
                               (0x10192, 0x0fce),
                               (0x101b7, 0x4da2),
                               (0x10206, 0x0fce),
                               (0x10227, 0x4da6),
                               (0x10258, 0x0fce)):
            with self.subTest(global_site=site):
                self.assertEqual(int.from_bytes(ram[base + site:
                                                     base + site + 2],
                                                'little'), expected)
        painter = ram[base + 0xff99:base + 0x102a0]
        self.assertEqual(painter.count(bytes.fromhex('9a0e002126')), 18)
        self.assertEqual(painter.count(bytes.fromhex('9a3e002126')), 21)
        # The resident move-to stores current pen X/Y directly. The
        # line-to first saves old X/Y, updates the current position and
        # calls the general 2621:0079 clipped line renderer.
        self.assertEqual(ram[0x26230:0x2623c],
                         bytes.fromhex('8b46088944128b4606894414'))
        self.assertEqual(ram[0x26266:0x26274],
                         bytes.fromhex('8b4608508944128b460650894414'))
        self.assertEqual(ram[0x26274:0x26279],
                         bytes.fromhex('9a79002126'))

    def test_palette_uploader_calls_original_retrace_wait(self):
        # GATE.RTL:f62d uses PUSH CS / near CALL with a negative displacement,
        # preserving the original bounded wait at GATE.RTL:f473.
        site = 0xf62e
        self.assertEqual(self.rtl[site - 1], 0x0e)
        self.assertEqual(self.rtl[site], 0xe8)
        delta = int.from_bytes(self.rtl[site + 1:site + 3],
                               'little', signed=True)
        self.assertEqual(site + 3 + delta, 0xf473)

    def test_image_entry_calls_original_helpers(self):
        for site, entry in ((0x12b32, 0x12c33),
                            (0x12b3d, 0x1266f),
                            (0x12b47, 0x12ed7),
                            (0x12b54, 0x131cb)):
            with self.subTest(callsite=site):
                self.assertEqual(self.rtl[site - 1], 0x0e)
                self.assertEqual(self.rtl[site], 0xe8)
                delta = int.from_bytes(self.rtl[site + 1:site + 3],
                                       'little', signed=True)
                self.assertEqual(site + 3 + delta, entry)

    def test_image_loader_calls_original_helpers(self):
        # Original resource open, conditional cleanup, and post-load setup.
        for site, entry in ((0x12c5d, 0x11ecb),
                            (0x12d49, 0x131cb),
                            (0x12d5a, 0x12205)):
            with self.subTest(callsite=site):
                self.assertEqual(self.rtl[site - 1], 0x0e)
                self.assertEqual(self.rtl[site], 0xe8)
                delta = int.from_bytes(self.rtl[site + 1:site + 3],
                                       'little', signed=True)
                self.assertEqual(site + 3 + delta, entry)

    def test_image_loader_relocated_far_services(self):
        # Original RTL has relocation placeholders. Private guest RAM is
        # necessary to verify which original services were actually linked.
        snapshot = ROOT / 'build/intro-analysis/at-22000.bin'
        if not snapshot.is_file():
            self.skipTest('private guest RAM snapshot unavailable')
        image = snapshot.read_bytes()
        base = GRAPHICS_TARGETS[0x0f10, 0x112e] - 0x112e
        for site, expected in ((0x12c8c, (0x145d, 0x00f7)),
                               (0x12ca3, (0x1479, 0x0002)),
                               (0x12d33, (0x0eec, 0x018a))):
            with self.subTest(callsite=site):
                self.assertEqual(self.rtl[site], 0x9a)
                addr = (0x0f10 + 0x1000) * 16 + site - base
                live = image[addr:addr + 5]
                self.assertEqual(live[0], 0x9a)
                linked = (int.from_bytes(live[3:5], 'little') - 0x1000,
                          int.from_bytes(live[1:3], 'little'))
                self.assertEqual(linked, expected)

    def test_image_loader_x_y_are_relocated_near_scalars(self):
        # The older native C reconstruction accidentally used DS:4dce/4dd0
        # segment selector WORDs as image coordinates. Original 8086 A1
        # instructions instead use two *near DS scalar* addresses.
        self.assertEqual(self.rtl[0x12cab:0x12cb0],
                         bytes.fromhex('a10a002689'))
        self.assertEqual(self.rtl[0x12cb2:0x12cb7],
                         bytes.fromhex('a10c002689'))
        snapshot = ROOT / 'build/intro-analysis/at-22000.bin'
        if not snapshot.is_file():
            self.skipTest('private guest RAM snapshot unavailable')
        ram = snapshot.read_bytes()
        base = GRAPHICS_TARGETS[0x0f10, 0x112e] - 0x112e
        for site, expected in ((0x12cab, b'\xa1\x20\x14'),
                               (0x12cb2, b'\xa1\x22\x14')):
            linked = (0x0f10 + 0x1000) * 16 + site - base
            self.assertEqual(ram[linked:linked + 3], expected)

    def test_original_resource_selector_and_far_services(self):
        # GATE.RTL:11ecb..120f2 has two return sites, and its next routine
        # begins at 120f3; unlike the other wrappers it does not end in RETF.
        self.assertEqual(self.rtl[0x11ecb:0x11ece], b'\x55\x8b\xec')
        self.assertEqual(self.rtl[0x11edf], 0xcb)
        self.assertEqual(self.rtl[0x1206f], 0xcb)
        self.assertEqual(self.rtl[0x120f3:0x120f6], b'\x55\x8b\xec')
        snapshot = ROOT / 'build/intro-analysis/at-22000.bin'
        if not snapshot.is_file():
            self.skipTest('private guest RAM snapshot unavailable')
        image = snapshot.read_bytes()
        base = GRAPHICS_TARGETS[0x0f10, 0x112e] - 0x112e
        for site, expected in ((0x11f3e, (0x0c33, 0x019a)),
                               (0x11f50, (0x0c33, 0x0040)),
                               (0x11f90, (0x0c33, 0x0091)),
                               (0x11fad, (0x0c33, 0x00f2)),
                               (0x12022, (0x0c33, 0x0091)),
                               (0x12033, (0x0c33, 0x00d4)),
                               (0x12047, (0x0c33, 0x00d4)),
                               (0x12094, (0x0c33, 0x0091)),
                               (0x120c7, (0x0c33, 0x00f2))):
            with self.subTest(callsite=site):
                self.assertEqual(self.rtl[site], 0x9a)
                addr = (0x0f10 + 0x1000) * 16 + site - base
                live = image[addr:addr + 5]
                self.assertEqual(live[0], 0x9a)
                linked = (int.from_bytes(live[3:5], 'little') - 0x1000,
                          int.from_bytes(live[1:3], 'little'))
                self.assertEqual(linked, expected)

    def test_original_image_postload_dependencies(self):
        # The post-load code calls exactly these recovered original near
        # helper boundaries, preserving all source-file identities.
        for site, target in ((0x1220c, 0x120f3),
                             (0x1221b, 0x12131),
                             (0x12249, 0x121b5),
                             (0x1225d, 0x121b5),
                             (0x122eb, 0x12383)):
            with self.subTest(near_call=site):
                self.assertEqual(self.rtl[site - 1], 0x0e)
                self.assertEqual(self.rtl[site], 0xe8)
                delta = int.from_bytes(self.rtl[site + 1:site + 3],
                                       'little', signed=True)
                self.assertEqual(site + 3 + delta, target)
        for start, exit_site in ((0x120f3, 0x12128),
                                 (0x12131, 0x121b4),
                                 (0x121b5, 0x121f4)):
            with self.subTest(helper=start):
                self.assertEqual(self.rtl[start:start + 3], b'\x55\x8b\xec')
                self.assertEqual(self.rtl[exit_site], 0xcb)

    def test_original_image_postload_relocated_services(self):
        snapshot = ROOT / 'build/intro-analysis/at-22000.bin'
        if not snapshot.is_file():
            self.skipTest('private guest RAM snapshot unavailable')
        image = snapshot.read_bytes()
        base = GRAPHICS_TARGETS[0x0f10, 0x112e] - 0x112e
        for site, expected in ((0x12161, (0x0cdc, 0x0430)),
                               (0x12197, (0x0cdc, 0x0396)),
                               (0x122ad, (0x1258, 0x172e))):
            with self.subTest(callsite=site):
                self.assertEqual(self.rtl[site], 0x9a)
                addr = (0x0f10 + 0x1000) * 16 + site - base
                live = image[addr:addr + 5]
                self.assertEqual(live[0], 0x9a)
                linked = (int.from_bytes(live[3:5], 'little') - 0x1000,
                          int.from_bytes(live[1:3], 'little'))
                self.assertEqual(linked, expected)

    def test_image_preparer_relocated_palette_service(self):
        snapshot = ROOT / 'build/intro-analysis/at-22000.bin'
        if not snapshot.is_file():
            self.skipTest('private guest RAM snapshot unavailable')
        image = snapshot.read_bytes()
        base = GRAPHICS_TARGETS[0x0f10, 0x112e] - 0x112e
        call_site = 0x126a6
        self.assertEqual(self.rtl[call_site], 0x9a)
        addr = (0x0f10 + 0x1000) * 16 + call_site - base
        self.assertEqual(image[addr:addr + 5], b'\x9a\xd2\x04\xdc\x1c')

    def test_original_relocated_image_data_references(self):
        # The original .RTL uses zero placeholders for many operand
        # displacements. Testing the actual linked image guards against
        # silently interpreting those as data at offset zero in C.
        snapshot = ROOT / 'build/intro-analysis/at-22000.bin'
        if not snapshot.is_file():
            self.skipTest('private guest RAM snapshot unavailable')
        image = snapshot.read_bytes()
        base = GRAPHICS_TARGETS[0x0f10, 0x112e] - 0x112e
        displacements = (
            (0x11ed4, 0x1416),  # resource selector busy flag
            (0x11eea, 0x4dce),  # video-mode segment
            (0x11eee, 0x0fce),  # video-mode word
            (0x11f37, 0x4dd6),  # file-handle segment
            (0x12679, 0x4dce),  # preparer video-mode segment
            (0x1267e, 0x0fce),  # preparer video-mode word
            (0x12688, 0x4dd4),  # preparer status segment
            (0x1268d, 0x0070),  # preparer status word
            (0x12693, 0x141c),  # palette upload pending flag
            (0x12735, 0x4dd2),  # preparer source-flags segment
            (0x1273a, 0x0082),  # preparer source-flags offset
            (0x12f08, 0x1418),  # image draw x origin
            (0x12f11, 0x141a),  # image draw y origin
            (0x12f7a, 0x4de6),  # animation video segment A
            (0x12f7f, 0x004c),  # animation video offset A
            (0x12f8b, 0x4de8),  # animation video segment B
            (0x12f90, 0x0042),  # animation video offset B
            (0x12f94, 0x4dea),  # animation video segment C
            (0x12f99, 0x003e),  # animation video offset C
        )
        for file_offset, expected in displacements:
            with self.subTest(original_rtl_offset=file_offset):
                addr = ((0x0f10 + 0x1000) * 16 + file_offset
                        - base)
                self.assertEqual(int.from_bytes(image[addr:addr + 2],
                                                 'little'), expected)

    def test_image_processor_original_calls(self):
        # Each address is the original RTL *file* offset, independently
        # checked against the already relocated DOS guest image.
        for site, entry in ((0x12f58, 0x12ed7),
                            (0x13142, 0x12ed7),
                            (0x1322c, 0x131cb)):
            with self.subTest(near_call=site):
                self.assertEqual(self.rtl[site - 1], 0x0e)
                self.assertEqual(self.rtl[site], 0xe8)
                delta = int.from_bytes(self.rtl[site + 1:site + 3],
                                       'little', signed=True)
                self.assertEqual(site + 3 + delta, entry)
        snapshot = ROOT / 'build/intro-analysis/at-22000.bin'
        if not snapshot.is_file():
            self.skipTest('private guest RAM snapshot unavailable')
        image = snapshot.read_bytes()
        base = GRAPHICS_TARGETS[0x0f10, 0x13d2] - 0x13d2
        for site, expected in ((0x12ee6, (0x0e48, 0x04d9)),
                               (0x12f17, (0x14a7, 0x000c)),
                               (0x12f1f, (0x0e48, 0x03c5)),
                               (0x13148, (0x02a5, 0x3b0c)),
                               (0x13170, (0x123f, 0x00b6))):
            with self.subTest(far_call=site):
                addr = (0x0f10 + 0x1000) * 16 + site - base
                live = image[addr:addr + 5]
                self.assertEqual(self.rtl[site], 0x9a)
                self.assertEqual(live[0], 0x9a)
                linked = (int.from_bytes(live[3:5], 'little') - 0x1000,
                          int.from_bytes(live[1:3], 'little'))
                self.assertEqual(linked, expected)

    def test_intro_animation_original_calls(self):
        # This controller has an early RETF at 1302b, and finishes with
        # a jump back into its load loop, not a final contiguous RETF.
        self.assertEqual(self.rtl[0x12f6b:0x12f6e], b'\x55\x8b\xec')
        self.assertEqual(self.rtl[0x1302b], 0xcb)
        self.assertEqual(self.rtl[0x130ab:0x130ae], b'\x55\x8b\xec')
        for site, entry in ((0x12f75, 0x12893),
                            (0x12fc3, 0x12c33),
                            (0x12fd0, 0x12ed7),
                            (0x12fd7, 0x12773),
                            (0x12fe1, 0x131cb),
                            (0x1302e, 0x1266f),
                            (0x13038, 0x12ed7),
                            (0x13045, 0x131cb),
                            (0x1309a, 0x12c33)):
            with self.subTest(near_call=site):
                self.assertEqual(self.rtl[site - 1], 0x0e)
                self.assertEqual(self.rtl[site], 0xe8)
                delta = int.from_bytes(self.rtl[site + 1:site + 3],
                                       'little', signed=True)
                self.assertEqual(site + 3 + delta, entry)

        snapshot = ROOT / 'build/intro-analysis/at-22000.bin'
        if not snapshot.is_file():
            self.skipTest('private guest RAM snapshot unavailable')
        image = snapshot.read_bytes()
        base = GRAPHICS_TARGETS[0x0f10, 0x1466] - 0x1466
        for site, expected in ((0x12f81, (0x0cdc, 0x0aac)),
                               (0x12fa3, (0x0cdc, 0x0aee)),
                               (0x12ff9, (0x02a5, 0x38c4)),
                               (0x1300a, (0x0000, 0x0008)),
                               (0x1304b, (0x02a5, 0x38c4)),
                               (0x13058, (0x02a5, 0x3b0c))):
            with self.subTest(far_call=site):
                addr = (0x0f10 + 0x1000) * 16 + site - base
                live = image[addr:addr + 5]
                self.assertEqual(self.rtl[site], 0x9a)
                self.assertEqual(live[0], 0x9a)
                linked = (int.from_bytes(live[3:5], 'little') - 0x1000,
                          int.from_bytes(live[1:3], 'little'))
                self.assertEqual(linked, expected)

    def test_intro_record_array_call_graph(self):
        # Loader and launcher remain FAR-returning original C functions.
        # Their array helpers are CS-prefixed near calls inside RTL:0f10.
        for site, target in ((0x12db5, 0x12c33),
                             (0x130d3, 0x13191),
                             (0x130ea, 0x12d67),
                             (0x130fb, 0x13115),
                             (0x1310b, 0x131f7)):
            with self.subTest(site=site):
                self.assertEqual(self.rtl[site - 1], 0x0e)
                self.assertEqual(self.rtl[site], 0xe8)
                delta = int.from_bytes(self.rtl[site + 1:site + 3],
                                       'little', signed=True)
                self.assertEqual(site + 3 + delta, target)

        snapshot = ROOT / 'build/intro-analysis/at-22000.bin'
        if not snapshot.is_file():
            self.skipTest('private guest RAM snapshot unavailable')
        image = snapshot.read_bytes()
        base = GRAPHICS_TARGETS[0x0f10, 0x15a6] - 0x15a6
        source_site = 0x130bd  # original oversized-count diagnostic
        self.assertEqual(self.rtl[source_site], 0x9a)
        addr = (0x0f10 + 0x1000) * 16 + source_site - base
        live = image[addr:addr + 5]
        self.assertEqual(live[0], 0x9a)
        self.assertEqual((int.from_bytes(live[3:5], 'little') - 0x1000,
                          int.from_bytes(live[1:3], 'little')),
                         (0x1258, 0x172e))

    def test_intro_graphics_segment_consistency(self):
        # Ten 0f10 calls share the same original file origin. Entries in
        # the independent 0cdc segment share another origin.
        by_segment = {}
        for (segment, linked), offset in GRAPHICS_TARGETS.items():
            by_segment.setdefault(segment, set()).add(offset - linked)
        self.assertEqual(by_segment[0x0f10], {0x11b05})
        self.assertEqual(by_segment[0x0cdc], {0xf149})

    def test_runtime_resolves_original_source_offsets(self):
        snapshot = ROOT / 'build/intro-analysis/at-22000.bin'
        if not snapshot.is_file():
            self.skipTest('private guest RAM snapshot unavailable')
        image = snapshot.read_bytes()
        matches = resolve_linked_origins(GRAPHICS_TARGETS, image, self.exe,
                                         self.rtl, 0x1000)
        for target, offset in GRAPHICS_TARGETS.items():
            with self.subTest(linked_far=target):
                self.assertIn(target, matches)
                self.assertEqual(matches[target]['file'], 'rtl')
                self.assertEqual(matches[target]['offset'], offset)
                self.assertEqual(matches[target]['matched_bytes'], 16)

    def test_fade_service_call_relocations(self):
        snapshot = ROOT / 'build/intro-analysis/at-22000.bin'
        if not snapshot.is_file():
            self.skipTest('private guest RAM snapshot unavailable')
        image = snapshot.read_bytes()
        base = GRAPHICS_TARGETS[0x0f10, 0x0c6e] - 0x0c6e
        # The original fade-in/fade-out/reset routines all dispatch
        # through the same RTL:0xf61b palette uploader. The one additional
        # 0x12893 call is the separate 100ms delay service.
        for callsite, original_far in ((0x127aa, (0x0cdc, 0x04d2)),
                                       (0x1280a, (0x0cdc, 0x04d2)),
                                       (0x12885, (0x0cdc, 0x04d2)),
                                       (0x1290f, (0x0cdc, 0x04d2)),
                                       (0x1291d, (0x123f, 0x00b6)),
                                       (0x1297e, (0x0cdc, 0x04d2)),
                                       (0x131e8, (0x145d, 0x016c))):
            with self.subTest(original_rtl_callsite=callsite):
                self.assertEqual(self.rtl[callsite], 0x9a)
                address = (0x0f10 + 0x1000) * 16 + callsite - base
                live = image[address:address + 5]
                self.assertEqual(live[0], 0x9a)
                linked = (int.from_bytes(live[3:5], 'little') - 0x1000,
                          int.from_bytes(live[1:3], 'little'))
                self.assertEqual(linked, original_far)


if __name__ == '__main__':
    unittest.main()
