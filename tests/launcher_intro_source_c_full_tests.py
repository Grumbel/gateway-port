#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Original C EXE:1302 full-run and frame-export regression.

Never includes or distributes game bytes; uses the private 22-second capture.
"""

import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
EXECUTABLE = None


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


class IntroFull(unittest.TestCase):
    def inputs(self):
        if EXECUTABLE is None:
            self.skipTest('standalone executable path not supplied')
        snapshot = ROOT / 'build/intro-analysis/at-22000.bin'
        assets = ROOT.parent / 'gateway-dos'
        if not snapshot.is_file() or not (assets / 'GATE_300.PIC').is_file():
            self.skipTest('private DOS RAM snapshot or original PIC absent')
        return str(EXECUTABLE), str(snapshot), str(assets)

    def test_shared_image_and_caption(self):
        executable, snapshot, assets = self.inputs()
        with tempfile.TemporaryDirectory() as directory:
            result = Path(directory) / 'combined.ppm'
            report = subprocess.run([executable, snapshot, assets, str(result)],
                                    check=True, capture_output=True, text=True)
            self.assertIn('background=64000 changed=29197, clock=19500 ms',
                          report.stdout)
            self.assertEqual(digest(result),
                             '42578f765802613954c301603c8ab14bb47a9679dd2f6f663219bce79372d3e5')

    def test_full_original_entry_and_replay(self):
        executable, snapshot, assets = self.inputs()
        with tempfile.TemporaryDirectory() as directory:
            final = Path(directory) / 'final.ppm'
            replay = Path(directory) / 'replay.gvr'
            report = subprocess.run([executable, snapshot, assets, str(final),
                                     '--full', '--record', str(replay)],
                                    check=True, capture_output=True, text=True)
            self.assertIn('original source-C EXE:1302 completed:', report.stdout)
            self.assertIn('342026 virtual ms', report.stdout)
            self.assertIn('816 replay frames', report.stdout)
            self.assertEqual(digest(final),
                             '6a3f6394fbaac476700dbb54d30a8dd0f9028183493cfa3f416d9e4a7c87a777')
            self.assertEqual(replay.stat().st_size, 4 + 816 * (4 + 64000 + 768))
            self.assertEqual(digest(replay),
                             '67cd026bf79e2425a9d41faac57439a7d49dca8559b127e7630319e43e112eb2')


if __name__ == '__main__':
    EXECUTABLE = sys.argv[1] if len(sys.argv) == 2 else None
    unittest.main(argv=[sys.argv[0]])
