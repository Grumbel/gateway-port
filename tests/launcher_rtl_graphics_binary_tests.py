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
