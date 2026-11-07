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
            final_ram = Path(directory) / 'final-guest-memory.bin'
            read_deps = Path(directory) / 'initial-read-dependencies.txt'
            report = subprocess.run([executable, snapshot, assets, str(final),
                                     '--full', '--record', str(replay),
                                     '--dump-memory', str(final_ram),
                                     '--read-deps', str(read_deps)],
                                    check=True, capture_output=True, text=True)
            self.assertIn('original source-C EXE:1302 completed:', report.stdout)
            self.assertIn('342026 virtual ms', report.stdout)
            self.assertIn('816 replay frames', report.stdout)
            self.assertEqual(digest(final),
                             '6a3f6394fbaac476700dbb54d30a8dd0f9028183493cfa3f416d9e4a7c87a777')
            self.assertEqual(replay.stat().st_size, 4 + 816 * (4 + 64000 + 768))
            self.assertEqual(digest(replay),
                             '67cd026bf79e2425a9d41faac57439a7d49dca8559b127e7630319e43e112eb2')
            self.assertEqual(final_ram.stat().st_size, 1024 * 1024)
            self.assertEqual(digest(final_ram),
                             '91703bb67fd1f07cf54aa9797e8d41700f68aa7aa4cebbb100e4c0424ee93c79')
            self.assertIn('first-read profile: 7429 guest bytes in 7 pages',
                          report.stdout)
            rows = read_deps.read_text().splitlines()
            self.assertEqual(rows[:8], [
                '# physical-page first-read-bytes (4096-byte pages)',
                '00000 768', '1b000 768', '2a000 10', '2b000 780',
                '2d000 573', '2e000 4096', '2f000 434',
            ])
            self.assertEqual(rows[8],
                             '# first-read physical byte ranges (end exclusive)')
            ranges = [(int(line.split()[1], 16), int(line.split()[2], 16))
                      for line in rows[9:] if line.startswith('range ')]
            self.assertGreater(len(ranges), 7)
            self.assertTrue(all(0 <= a < b <= 1024 * 1024
                                for a, b in ranges))
            self.assertTrue(all(a >= prior_b for (_, prior_b), (a, _)
                                in zip(ranges, ranges[1:])))
            self.assertEqual(sum(b - a for a, b in ranges), 7429)

    def test_sparse_guest_ram_preserves_full_video(self):
        executable, snapshot, assets = self.inputs()
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            profile = directory / 'read-pages.txt'
            baseline = directory / 'baseline.ppm'
            baseline_record = directory / 'baseline.gvr'
            sparse = directory / 'sparse.bin'
            result = directory / 'sparse.ppm'
            sparse_record = directory / 'sparse.gvr'
            subprocess.run([executable, snapshot, assets, str(baseline),
                            '--full', '--read-deps', str(profile),
                            '--record', str(baseline_record)],
                           check=True, capture_output=True, text=True)
            page_offsets = {
                int(line.split()[0], 16)
                for line in profile.read_text().splitlines()
                if line and not line.startswith(('#', 'range '))
            }
            self.assertEqual(len(page_offsets), 7)
            ram = bytearray(Path(snapshot).read_bytes())
            self.assertEqual(len(ram), 1024 * 1024)
            blank = bytes(4096)
            for offset in range(0, len(ram), 4096):
                if offset not in page_offsets or offset == 0x2a000:
                    ram[offset:offset + 4096] = blank
            sparse.write_bytes(ram)  # Private derived guest data stays in tmp.
            subprocess.run([executable, str(sparse), assets, str(result),
                            '--full', '--record', str(sparse_record)],
                           check=True, capture_output=True, text=True)
            self.assertEqual(digest(result), digest(baseline))
            self.assertEqual(digest(result),
                             '6a3f6394fbaac476700dbb54d30a8dd0f9028183493cfa3f416d9e4a7c87a777')
            self.assertEqual(digest(sparse_record), digest(baseline_record))
            self.assertEqual(digest(sparse_record),
                             '67cd026bf79e2425a9d41faac57439a7d49dca8559b127e7630319e43e112eb2')

    def test_original_exe_contains_the_static_intro_text_data(self):
        _, snapshot, assets = self.inputs()
        ram = Path(snapshot).read_bytes()
        exe = (Path(assets) / 'GATE.EXE').read_bytes()
        # The 22-second snapshot carries an unmodified contiguous copy of
        # these original EXE data bytes; no text is embedded in this test.
        # It spans the 4,995-byte first-read run 2dddb:2f15e.
        self.assertEqual(ram[0x2dd9d:0x2f48a], exe[0x4251:0x593e])
        self.assertEqual(0x2f48a - 0x2dd9d, 5869)

        # Another complete 768-byte read range comes from the original
        # RTL, except for 23 paired 16-bit fixups applied by its loader.
        rtl = (Path(assets) / 'GATE.RTL').read_bytes()
        linked = ram[0x1b77c:0x1ba7c]
        raw = rtl[0xd7bf:0xdabf]
        self.assertEqual(len(linked), len(raw))
        offsets = [i for i, (a, b) in enumerate(zip(linked, raw)) if a != b]
        self.assertEqual(len(offsets), 46)
        self.assertTrue(all(raw[i] == 0 for i in offsets))
        self.assertTrue(all(offsets[i + 1] == offsets[i] + 1
                            for i in range(0, len(offsets), 2)))


if __name__ == '__main__':
    EXECUTABLE = sys.argv[1] if len(sys.argv) == 2 else None
    unittest.main(argv=[sys.argv[0]])
