#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Check the recovered C function boundaries and calls against original x86.

This reads the user's GATE.EXE; no original executable bytes or story text
are embedded in the source or tests. Host C mock tests are separate.
"""

import hashlib
from pathlib import Path
import re
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from inspect_intro_program import inspect_intro_program


ROOT = Path(__file__).resolve().parents[1]
EXPECTED_SHA256 = '767ccbbacbd6b304a5bab072894a45ca426a2196ae369b2f11d02ef92bb7134b'


def u16(exe, offset):
    return struct.unpack_from('<H', exe, offset)[0]


def far_call(exe, site, segment, offset):
    assert exe[site] == 0x9a, f'not a far call at 0x{site:x}'
    assert (u16(exe, site + 3), u16(exe, site + 1)) == (segment, offset)


def near_call(exe, site, target):
    assert exe[site] == 0xe8, f'not a near call at 0x{site:x}'
    displacement = struct.unpack_from('<h', exe, site + 1)[0]
    assert site + 3 + displacement == target


class LauncherDecompileBinaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = ROOT.parent / 'gateway-dos' / 'GATE.EXE'
        if not path.exists():
            raise unittest.SkipTest('original Gateway DOS data not available')
        cls.exe = path.read_bytes()
        if hashlib.sha256(cls.exe).hexdigest() != EXPECTED_SHA256:
            raise unittest.SkipTest('different launcher executable: identities unverified')

    def test_function_boundaries_and_far_returns(self):
        exe = self.exe
        for start, end in ((0x0e37, 0x0f17), (0x0f18, 0x0f60),
                           (0x1c11, 0x1c6b), (0x1c6c, 0x1caa)):
            self.assertEqual(exe[start], 0x55)  # push bp
            self.assertEqual(exe[end], 0xcb)    # retf

    def test_input_handler_call_graph(self):
        exe = self.exe
        for site, seg, off in (
            (0x0e42, 0x0df2, 0x000a), (0x0e4e, 0x0df2, 0x0030),
            (0x0e5b, 0x123f, 0x0004), (0x0e83, 0x0c4d, 0x018d),
            (0x0e98, 0x0c4d, 0x061b), (0x0ea5, 0x0c4d, 0x05be),
            (0x0eb0, 0x0c4d, 0x0511), (0x0ed3, 0x0c4d, 0x0653),
            (0x0ee0, 0x0c4d, 0x0698), (0x0eff, 0x0df2, 0x00f8),
            (0x0f06, 0x0e48, 0x07fe),
        ):
            far_call(exe, site, seg, off)
        near_call(exe, 0x0ef9, 0x0f18)
        self.assertEqual(u16(exe, 0x0ef5), 3500)

    def test_interruptible_clock_uses_signed_high_word(self):
        exe = self.exe
        far_call(exe, 0x0f23, 0x02a5, 0x38c4)
        far_call(exe, 0x0f3a, 0x02a5, 0x38c4)
        far_call(exe, 0x0f4b, 0x0df2, 0x000a)
        far_call(exe, 0x0f54, 0x0e48, 0x07fe)
        self.assertEqual(exe[0x0f42], 0x7f)  # JG: signed high word
        self.assertEqual(exe[0x0f44], 0x7c)  # JL: signed high word
        self.assertEqual(exe[0x0f49], 0x73)  # JAE: unsigned low word

    def test_palette_loops(self):
        exe = self.exe
        far_call(exe, 0x1c1c, 0x0df2, 0x000a)
        far_call(exe, 0x1c4c, 0x0cdc, 0x04d2)
        far_call(exe, 0x1c96, 0x0cdc, 0x04d2)
        near_call(exe, 0x1c62, 0x0f18)
        self.assertEqual(u16(exe, 0x1c26), 3)      # signed IMUL operand
        self.assertEqual(u16(exe, 0x1c2c), 45)     # DAC component offset
        self.assertEqual(exe[0x1c5a], 63)         # inclusive loop
        self.assertEqual(u16(exe, 0x1c5e), 500)   # final delay
        self.assertEqual(exe[0x1ca4], 64)         # exclusive loop

    def test_complete_enclosing_intro_function_exits(self):
        exe = self.exe
        self.assertEqual(exe[0x1302:0x1304], b'\x33\xc0')
        far_call(exe, 0x1304, 0x02a5, 0x0c08)  # compiler stack check
        self.assertEqual(exe[0x1a13], 0xcb)       # one RETF
        self.assertEqual(exe[0x1398], 0x7d)       # JGE (signed) into long intro
        self.assertEqual(exe[0x1399] + 0x139a, 0x13fc)
        self.assertEqual(exe[0x1392:0x1398], b'\x26\x83\x3e\xce\x0f\x02')
        self.assertEqual(exe[0x13f9], 0xe9)       # short path bypasses cleanup
        self.assertEqual(0x13fc + struct.unpack_from('<h', exe, 0x13fa)[0], 0x1a0e)
        self.assertEqual(exe[0x1404], 0xe9)       # Escape uses cleanup
        self.assertEqual(0x1407 + struct.unpack_from('<h', exe, 0x1405)[0], 0x1a09)
        near_call(exe, 0x13fd, 0x0e37)
        near_call(exe, 0x1a06, 0x0e37)
        far_call(exe, 0x1a09, 0x0f10, 0x0d8e)
        far_call(exe, 0x1a0e, 0x0df2, 0x0128)

    def test_original_mode_dependent_palette_interlude(self):
        exe = self.exe
        self.assertEqual(exe[0x1a14:0x1a17], b'\x55\x8b\xec')
        self.assertEqual(exe[0x1c10], 0xcb)
        near_call(exe, 0x138b, 0x1a14)
        far_call(exe, 0x1a3c, 0x0f10, 0x0c6e)
        far_call(exe, 0x1a54, 0x0f10, 0x1014)
        far_call(exe, 0x1b3e, 0x0f10, 0x1014)
        for call in (0x1a7a, 0x1aa3, 0x1ae5, 0x1b0e,
                     0x1b64, 0x1b8d, 0x1bcc, 0x1bf5):
            far_call(exe, call, 0x0cdc, 0x04d2)
        for cmp_at, branch_at in ((0x1a60, 0x1a66),
                                  (0x1acb, 0x1ad1),
                                  (0x1b4a, 0x1b50),
                                  (0x1bb2, 0x1bb8)):
            self.assertEqual(exe[cmp_at:cmp_at + 6],
                             b'\x26\x83\x3e\xce\x0f\x02')
            self.assertEqual(exe[branch_at], 0x7d)  # signed JGE
        for site in (0x1abb, 0x1b2a, 0x1ba5):
            far_call(exe, site, 0x0df2, 0x000a)
        for site in (0x1a46, 0x1ab5, 0x1b24, 0x1b9f, 0x1c07):
            near_call(exe, site, 0x0f18)
        self.assertEqual(exe[0x1a36:0x1a3b], b'\x80\x7e\xfe\x2d\x7c')

    def test_original_picture_palette_fade_arithmetic(self):
        exe = self.exe
        self.assertEqual(exe[0x0f61:0x0f64], b'\x55\x8b\xec')
        self.assertEqual(exe[0x1039], 0xcb)
        for site, off in ((0x0f6d, 0x0d8e), (0x0f78, 0x1680),
                          (0x0f8c, 0x112e), (0x0fbf, 0x13d2),
                          (0x0fcd, 0x16c6)):
            far_call(exe, site, 0x0f10, off)
        for site in (0x0fb1, 0x1013, 0x102d):
            far_call(exe, site, 0x0cdc, 0x04d2)
        self.assertEqual(u16(exe, 0x0f65), 0x038c)  # local stack size
        self.assertEqual(u16(exe, 0x0fa7), 0x0300)  # zero palette size
        self.assertEqual(u16(exe, 0x1009), 0x0300)  # 768 components per step
        self.assertEqual(exe[0x0feb:0x0ff0], b'\x98\xf7\x6e\xfe\x99')
        self.assertEqual(exe[0x0ff4:0x0ff9], b'\xb9\x06\x00\xd3\xf8')
        self.assertEqual(exe[0x101b:0x1025],
                         b'\x83\x46\xfe\x04\x83\x7e\xfe\x40\x7c\xb5')

    def test_two_pass_caption_layout_original_code(self):
        exe = self.exe
        for start, end, frame in ((0x103a, 0x117d, 0x60),
                                  (0x117e, 0x1301, 0x62)):
            self.assertEqual(exe[start:start + 3], b'\x55\x8b\xec')
            self.assertEqual(exe[start + 3], 0xb8)
            self.assertEqual(u16(exe, start + 4), frame)
            far_call(exe, start + 6, 0x02a5, 0x0c08)
            self.assertEqual(exe[end], 0xcb)

        # Both functions use the same BP-0x5a text-buffer offset; the
        # caller's frame also stores a bottom word before the 80-byte area.
        self.assertEqual(exe[0x1091:0x1094], b'\x8d\x46\xa6')
        self.assertEqual(exe[0x1217:0x121a], b'\x8d\x46\xa6')
        # BP+6, +10, +14 are FAR pointers to left/top/right in the helper.
        self.assertEqual(exe[0x1052:0x1055], b'\xc4\x5e\x06')
        self.assertEqual(exe[0x1059:0x105c], b'\xc4\x5e\x0a')
        self.assertEqual(exe[0x1060:0x1063], b'\xc4\x5e\x0e')
        self.assertEqual(exe[0x10e7], 0x7e)  # signed JLE wrap test
        self.assertEqual(exe[0x126a], 0x7e)  # signed JLE in print pass
        self.assertEqual(u16(exe, 0x1163), 185)
        self.assertEqual(exe[0x1165], 0x7e)  # signed JLE vertical clamp

        near_call(exe, 0x11c5, 0x103a)
        self.assertEqual(exe[0x11c8:0x11cb], b'\x83\xc4\x14')
        for site, offset in ((0x104a, 0x018d), (0x10d0, 0x05be),
                             (0x10f2, 0x0511), (0x112f, 0x0524),
                             (0x118d, 0x018d), (0x11a2, 0x061b),
                             (0x1256, 0x05be), (0x1272, 0x0511),
                             (0x1280, 0x0653), (0x128d, 0x0698),
                             (0x12b5, 0x0511), (0x12c7, 0x0524)):
            far_call(exe, site, 0x0c4d, offset)
        far_call(exe, 0x11eb, 0x0cdc, 0x0e50)
        far_call(exe, 0x12e9, 0x02a5, 0x2bba)
        near_call(exe, 0x12fb, 0x0f18)
        self.assertEqual(exe[0x12f1:0x12f9],
                         b'\xb9\x2d\x00\xf7\xe1\x05\xd0\x07')

    def test_transcribed_c_captions_and_animation_arguments(self):
        exe = self.exe
        inspection = inspect_intro_program(exe)
        source = (ROOT / 'decomp/launcher/gate_exe.c').read_text()
        source = source.split('void gate_exe_play_intro(void)', 1)[1]
        source = source.split('void gate_exe_play_title_interlude(void)', 1)[0]

        # The C transcription now uses descriptive names, while this oracle
        # independently reads the untouched original machine-code operands.
        # Resolve names from the source headers, preserving the literal
        # original resource IDs, DS offsets and counts in their declarations.
        headers = ''.join((ROOT / path).read_text() for path in (
            'decomp/launcher/gate_intro_schedule.h',
            'decomp/launcher/gate_memory_layout.h',
        ))
        named_values = {
            name: int(value, 0)
            for name, value in re.findall(
                r'^\s*(GATE_(?:DS_INTRO_CAPTION|INTRO_PICTURE|INTRO_)[A-Z0-9_]+)'
                r'\s*=\s*(0x[0-9a-f]+|\d+)\s*,', headers, re.MULTILINE)
        }

        def original_operand(token):
            return named_values[token] if token.startswith('GATE_') else int(token, 0)

        captions = re.findall(
            r'gate_exe_show_caption\((\d+), (\d+), (\d+), '
            r'far_pointer\(gate_ds_segment\(\), (GATE_DS_INTRO_CAPTION_\d+)\)\);', source)
        found_captions = [(original_operand(ptr), int(left), int(top), int(right))
                          for left, top, right, ptr in captions]
        original_captions = [
            (row['text_pointer'], row['left'], row['requested_top'], row['right'])
            for row in inspection['captions']]
        self.assertEqual(found_captions, original_captions)

        original_bars = [row['bar'] for row in inspection['logo_palette_steps']]
        c_bars = [int(index) for index in re.findall(
            r'gate_exe_reveal_red_bar\((\d+)\);', source)]
        self.assertEqual(c_bars, original_bars)

        original_animations = [
            (row['base_resource'], row['next_resource'], row['count'])
            for row in inspection['animations']]
        c_animations = [tuple(original_operand(value) for value in match)
                        for match in re.findall(
                            r'gate_linked_play_intro_delta_sequence\('
                            r'\s*(GATE_INTRO_PICTURE_[A-Z0-9_]+),'
                            r'\s*(GATE_INTRO_PICTURE_[A-Z0-9_]+),'
                            r'\s*(GATE_INTRO_[A-Z0-9_]+)\);', source)]
        self.assertEqual(c_animations, original_animations)

        original_frames = []
        for site in (0x1597, 0x1633, 0x1698, 0x16d1, 0x1833,
                     0x1874, 0x18d4, 0x196a, 0x19ab):
            near_call(exe, site, 0x0f61)
            self.assertEqual(exe[site - 5], 0xb8)
            self.assertEqual(exe[site - 2:site], b'\x50\x0e')
            original_frames.append(u16(exe, site - 4))
        c_frames = [original_operand(value) for value in re.findall(
            r'gate_exe_fade_in_picture\((GATE_INTRO_PICTURE_[A-Z0-9_]+)\);', source)]
        self.assertEqual(c_frames, original_frames)


if __name__ == '__main__':
    unittest.main()
