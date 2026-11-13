#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Checks the 16-bit x86 decoder and its byte-exact NASM formatting."""
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
import x86_16 as X  # noqa: E402

# Hand-written encodings, including MSC-specific direction-bit forms.
SAMPLES = [
    bytes.fromhex('8bc3'),          # mov ax, bx (8B form; NASM picks 89)
    bytes.fromhex('03c3'),          # add ax, bx (03 form)
    bytes.fromhex('83c002'),        # add ax, byte 2
    bytes.fromhex('050500'),        # add ax, strict word 5
    bytes.fromhex('8b4600'),        # mov ax, [bp+0] with explicit disp8
    bytes.fromhex('8b870300'),      # mov ax, [bx+3] with disp16
    bytes.fromhex('26a10200'),      # mov ax, [es:2]
    bytes.fromhex('f3a4'),          # rep movsb
    bytes.fromhex('c45efc'),        # les bx, [bp-4]
    bytes.fromhex('6a05'),          # push byte 5 (186)
    bytes.fromhex('c8040000'),      # enter 4, 0
    bytes.fromhex('9a5b10cd17'),    # call far 17cd:105b
    bytes.fromhex('ff5e06'),        # call far [bp+6]
    bytes.fromhex('d1e0'),          # shl ax, 1
    bytes.fromhex('c1e004'),        # shl ax, 4 (186)
    bytes.fromhex('2eff2e3412'),    # jmp far [cs:0x1234]
]


class DecodeTests(unittest.TestCase):
    def test_lengths(self):
        for raw in SAMPLES:
            ins = X.decode(raw, 0)
            self.assertEqual(ins.length, len(raw), raw.hex())
            self.assertEqual(ins.raw, raw)

    def test_flow(self):
        self.assertEqual(X.decode(bytes(0x10) + bytes.fromhex('e80300'), 0x10).target, 0x16)
        self.assertEqual(X.decode(bytes.fromhex('74fe'), 0).flow, 'jcc')
        self.assertEqual(X.decode(bytes.fromhex('74fe'), 0).target, 0)
        ins = X.decode(bytes.fromhex('9a5b10cd17'), 0)
        self.assertEqual((ins.flow, ins.far), ('callfar', X.Far(0x17cd, 0x105b)))
        self.assertEqual(X.decode(bytes.fromhex('cb'), 0).flow, 'ret')
        self.assertEqual(X.decode(bytes.fromhex('ffe0'), 0).flow, 'jmpind')

    def test_fp_emulator_interrupt_consumes_modrm(self):
        # INT 35h is the MSC emulator's D9 escape: modrm + disp follow.
        ins = X.decode(bytes.fromhex('cd35460a'), 0)
        self.assertEqual((ins.length, ins.note), (4, 'fp-emulator'))
        with self.assertRaises(X.Unformattable):
            X.format_nasm(ins)

    def test_truncated(self):
        with self.assertRaises(X.DecodeError):
            X.decode(bytes.fromhex('8b'), 0)

    def test_labels(self):
        ins = X.decode(bytes.fromhex('eb02'), 0)
        self.assertEqual(X.format_nasm(ins, {4: 'L_4'}.get), 'jmp short L_4')
        with self.assertRaises(X.Unformattable):
            X.format_nasm(ins)

    @unittest.skipUnless(shutil.which('nasm'), 'nasm not installed')
    def test_reassembles(self):
        texts = [X.format_nasm(X.decode(raw, 0)) for raw in SAMPLES]
        with tempfile.TemporaryDirectory() as tmp:
            src = pathlib.Path(tmp, 't.asm')
            src.write_text('bits 16\n' + X.NASM_PRELUDE + '\n' + '\n'.join(texts) + '\n')
            subprocess.run(['nasm', '-f', 'bin', '-o', str(pathlib.Path(tmp, 't.bin')),
                            str(src)], check=True)
            self.assertEqual(pathlib.Path(tmp, 't.bin').read_bytes(), b''.join(SAMPLES))


if __name__ == '__main__':
    unittest.main()
