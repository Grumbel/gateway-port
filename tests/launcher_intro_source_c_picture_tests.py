#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""End-to-end original PIC C-loader output, requiring private user assets."""

import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
EXECUTABLE = None


class IntroPicture(unittest.TestCase):
    def test_original_image(self):
        if EXECUTABLE is None:
            self.skipTest('standalone executable path not supplied')
        executable = Path(EXECUTABLE)
        snapshot = ROOT / 'build/intro-analysis/at-22000.bin'
        assets = ROOT.parent / 'gateway-dos'
        if not snapshot.is_file() or not (assets / 'GATE_300.PIC').is_file():
            self.skipTest('private DOS RAM snapshot or original PIC absent')
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'image.pgm'
            result = subprocess.run([str(executable), str(snapshot),
                                     str(assets), str(output)],
                                    check=True, capture_output=True, text=True)
            self.assertIn('2589 nonzero VGA pixels', result.stdout)
            frame = output.read_bytes()
            self.assertEqual(len(frame), 320 * 200 + len(b'P5\n320 200\n255\n'))
            self.assertEqual(hashlib.sha256(frame).hexdigest(),
                             'f89abadd3f12a5e014b5c09f227d528680a3f990b307d721b3e76b8beed1f50f')
            pixels = frame[len(b'P5\n320 200\n255\n'):]
            occupied = [(i % 320, i // 320) for i, value in enumerate(pixels) if value]
            self.assertEqual(len(occupied), 2589)
            self.assertEqual((min(x for x, _ in occupied),
                              max(x for x, _ in occupied),
                              min(y for _, y in occupied),
                              max(y for _, y in occupied)), (24, 293, 81, 118))


if __name__ == '__main__':
    EXECUTABLE = sys.argv[1] if len(sys.argv) == 2 else None
    unittest.main(argv=[sys.argv[0]])
