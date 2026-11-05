#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Synthetic validation of the native launcher-script inspector."""

from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from inspect_intro_program import (CAPTION_COUNT, FIRST_TEXT, caption_call_sites,
                                   inspect_animation_calls, inspect_intro_program,
                                   inspect_logo_palette_calls, parse_caption_arguments)


def synthetic_executable():
    exe = bytearray(0x5000)
    exe[:2] = b'MZ'
    exe[0x12e9:0x12ee] = bytes.fromhex('9aba2ba502')
    exe[0x12f1:0x12fe] = bytes.fromhex('b92d00f7e105d007500ee81afc')
    position = FIRST_TEXT
    for page in range(CAPTION_COUNT):
        string = bytes([ord('A') + (page % 26)]) * (page + 1)
        exe[position:position + len(string) + 1] = string + b'\0'
        ptr = 0x2000 + position - FIRST_TEXT
        code = struct.pack('<BH', 0xb8, ptr) + b'\x1e\x50'
        code += struct.pack('<BH', 0xb8, 310) + b'\x50'
        code += struct.pack('<BH', 0xb8, 10) + b'\x50'
        if page == 0:
            code += b'\x50'  # repeated AX pushes are used in the original
        else:
            code += struct.pack('<BH', 0xb8, 10) + b'\x50'
        call = 0x1500 + page * 0x24 + len(code)
        code += b'\x0e\xe8' + struct.pack('<h', 0x117e - (call + 4))
        begin = 0x1500 + page * 0x24
        exe[begin:begin + len(code)] = code
        position += len(string) + 1
    return exe


class IntroProgramTests(unittest.TestCase):
    def test_all_synthetic_caption_calls_and_text_lengths(self):
        exe = synthetic_executable()
        report = inspect_intro_program(exe)
        self.assertEqual(len(report['captions']), CAPTION_COUNT)
        self.assertEqual(len(caption_call_sites(exe)), CAPTION_COUNT)
        for i, row in enumerate(report['captions']):
            self.assertEqual(row['text_length'], i + 1)
            self.assertEqual(row['display_ms'], 2000 + 45 * (i + 1))
            self.assertEqual((row['left'], row['requested_top'], row['right']),
                             (10, 10, 310))

    def test_wrong_call_target_rejected(self):
        exe = synthetic_executable()
        call = caption_call_sites(exe)[0]
        struct.pack_into('<h', exe, call + 2, 0)
        with self.assertRaisesRegex(ValueError, 'expected 18 intro calls'):
            inspect_intro_program(exe)

    def test_pointer_order_checked(self):
        exe = synthetic_executable()
        call = caption_call_sites(exe)[4]
        exe[call - 16] ^= 1  # source pointer's low byte
        with self.assertRaisesRegex(ValueError, 'non-sequential text pointer'):
            inspect_intro_program(exe)

    def test_reject_invalid_placement(self):
        exe = synthetic_executable()
        call = caption_call_sites(exe)[1]
        # `mov AX,10` just before the final push: force left > right.
        struct.pack_into('<H', exe, call - 3, 320)
        with self.assertRaisesRegex(ValueError, 'dimensions out of range'):
            parse_caption_arguments(exe, call)

    def test_synthetic_animation_and_logo_calls(self):
        exe = synthetic_executable()
        begin = 0x1800
        values = (32, 0x3201, 0x3200)
        code = b''.join(struct.pack('<BH', 0xb8, value) + b'\x50'
                        for value in values)
        exe[begin:begin + 17] = code + b'\x9a\x66\x14\x10\x0f'
        bar = 0x1820
        exe[bar:bar + 8] = (struct.pack('<BH', 0xb8, 7) + b'\x50\x0e\xe8' +
                            struct.pack('<h', 0x1c11 - (bar + 8)))
        rows = inspect_animation_calls(exe)
        self.assertEqual(len(rows), 1)
        self.assertEqual((rows[0]['pic_file'], rows[0]['base_entry'],
                          rows[0]['next_entry'], rows[0]['count']),
                         (302, 0, 1, 32))
        self.assertEqual(inspect_logo_palette_calls(exe)[0]['dac_palette_index'], 66)
        # A parameter change is not allowed to silently produce a new frame range.
        exe[begin + 10] = 0x31  # malformed source resource prefix
        with self.assertRaisesRegex(ValueError, 'invalid picture sequence'):
            inspect_animation_calls(exe)

    def test_optional_original_game(self):
        path = ROOT.parent / 'gateway-dos' / 'GATE.EXE'
        if not path.is_file():
            self.skipTest('original proprietary executable is not available')
        report = inspect_intro_program(path.read_bytes())
        rows = report['captions']
        self.assertEqual((rows[0]['call_file_offset'], rows[0]['display_ms']),
                         (0x15c4, 19415))
        self.assertEqual((rows[-1]['call_file_offset'], rows[-1]['display_ms']),
                         (0x19fe, 20585))
        self.assertEqual(sum(row['text_length'] for row in rows), 4977)
        animations = report['animations']
        self.assertEqual([row['call_file_offset'] for row in animations],
                         [0x1743, 0x1762, 0x191f, 0x1933, 0x1947])
        self.assertEqual([(row['pic_file'], row['base_entry'], row['next_entry'],
                           row['count']) for row in animations],
                         [(303, 42, 43, 37), (302, 33, 61, 42),
                          (302, 0, 1, 32), (302, 33, 34, 27), (303, 0, 1, 41)])
        self.assertEqual([row['bar'] for row in report['logo_palette_steps']],
                         [1, 6, 4, 7, 2, 5, 3])


if __name__ == '__main__':
    unittest.main()
