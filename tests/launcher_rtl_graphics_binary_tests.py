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
