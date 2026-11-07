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
# Verified read-only resident RTL spans (physical end exclusive). The gaps
# contain mutable loader/runtime state and must not be blanket-restored.
RTL_RESIDENT_RANGES = (
    (0x2b730, 0x2b755), (0x2ba9e, 0x2bae2),
    (0x2bbb8, 0x2bbbc), (0x2bbc4, 0x2bbc8),
    (0x2bc23, 0x2bc2a), (0x2d05e, 0x2d06e),
    (0x2b778, 0x2b77c),
)


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
            self.assertIn('1561 replay frames', report.stdout)
            self.assertEqual(digest(final),
                             '6a3f6394fbaac476700dbb54d30a8dd0f9028183493cfa3f416d9e4a7c87a777')
            self.assertEqual(replay.stat().st_size, 4 + 1561 * (4 + 64000 + 768))
            self.assertEqual(digest(replay),
                             '856737f003f9af3a1ce4fdd3f7e494ee22b4cb7b9db36c728a6c9d57ad088ccf')
            self.assertEqual(final_ram.stat().st_size, 1024 * 1024)
            self.assertEqual(digest(final_ram),
                             '91703bb67fd1f07cf54aa9797e8d41700f68aa7aa4cebbb100e4c0424ee93c79')
            self.assertIn('first-read profile: 6661 guest bytes in 6 pages',
                          report.stdout)
            rows = read_deps.read_text().splitlines()
            self.assertEqual(rows[:7], [
                '# physical-page first-read-bytes (4096-byte pages)',
                '00000 768', '2a000 10', '2b000 780',
                '2d000 573', '2e000 4096', '2f000 434',
            ])
            self.assertEqual(rows[7],
                             '# first-read physical byte ranges (end exclusive)')
            ranges = [(int(line.split()[1], 16), int(line.split()[2], 16))
                      for line in rows[8:] if line.startswith('range ')]
            self.assertGreater(len(ranges), 7)
            self.assertTrue(all(0 <= a < b <= 1024 * 1024
                                for a, b in ranges))
            self.assertTrue(all(a >= prior_b for (_, prior_b), (a, _)
                                in zip(ranges, ranges[1:])))
            self.assertEqual(sum(b - a for a, b in ranges), 6661)

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
            self.assertEqual(len(page_offsets), 6)
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
                             '856737f003f9af3a1ce4fdd3f7e494ee22b4cb7b9db36c728a6c9d57ad088ccf')

            # Prove that the largest guest input block can be restored
            # from the user's original EXE rather than a Unicorn capture.
            stripped = directory / 'stripped-exe-data.bin'
            stripped_final = directory / 'restored.ppm'
            stripped_record = directory / 'restored.gvr'
            ram[0x2dd9d:0x2f48a] = bytes(5869)
            stripped.write_bytes(ram)
            subprocess.run([executable, str(stripped), assets,
                            str(stripped_final), '--full',
                            '--restore-exe-data', '--record',
                            str(stripped_record)],
                           check=True, capture_output=True, text=True)
            self.assertEqual(digest(stripped_final), digest(baseline))
            self.assertEqual(digest(stripped_record), digest(baseline_record))

            # The RTL's linked bank also comes from the original file,
            # except the 23 16-bit operands written by the DOS relocator.
            # Keep only those fixups from the RAM image; restore all other
            # nonzero bytes directly from the user's original RTL file.
            rtl_bytes = (Path(assets) / 'GATE.RTL').read_bytes()[0xd7bf:0xdabf]
            self.assertEqual(len(rtl_bytes), 768)
            for offset, value in enumerate(rtl_bytes):
                if value:
                    ram[0x1b77c + offset] = 0
            stripped_rtl = directory / 'stripped-rtl-data.bin'
            restored_rtl_final = directory / 'restored-rtl.ppm'
            restored_rtl_record = directory / 'restored-rtl.gvr'
            stripped_rtl.write_bytes(ram)
            subprocess.run([executable, str(stripped_rtl), assets,
                            str(restored_rtl_final), '--full',
                            '--restore-exe-data', '--restore-rtl-data',
                            '--record', str(restored_rtl_record)],
                           check=True, capture_output=True, text=True)
            self.assertEqual(digest(restored_rtl_final), digest(baseline))
            self.assertEqual(digest(restored_rtl_record), digest(baseline_record))

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

        # Eleven original MOV DX,imm16 palette segments in the raw EXE are
        # patched by the DOS loader from 1a7a to 2a7a (load base 1000h).
        # The C port must use the *linked* palette-bank selector, not the
        # raw immediate that used to read unrelated RTL code as a palette.
        raw_sites = [pos for pos in range(len(exe) - 3)
                     if exe[pos] == 0xba and
                     int.from_bytes(exe[pos + 1:pos + 3], 'little') == 0x1a7a]
        self.assertEqual(len(raw_sites), 11)
        for position in raw_sites:
            linked_position = position + 0xf466
            self.assertEqual(ram[linked_position], 0xba)
            self.assertEqual(int.from_bytes(ram[linked_position + 1:
                                                 linked_position + 3], 'little'),
                             0x2a7a)

        # Static RTL resource metadata and presentation tables also survive
        # byte-for-byte in a *second* original RTL resident memory bank.
        for start, end in RTL_RESIDENT_RANGES:
            disk_start = start - 0x2a777
            self.assertEqual(ram[start:end],
                             rtl[disk_start:disk_start + end - start])

        # The original launcher has a 105-entry resident FAR selector table.
        # Every word names either the captured DS or the adjacent bank ten
        # paragraphs below it, with 32 entries naming DS itself.
        selectors = [int.from_bytes(ram[0x2f520 + i:0x2f522 + i], 'little')
                     for i in range(0, 210, 2)]
        self.assertEqual(len(selectors), 105)
        self.assertEqual(set(selectors), {0x2a7a, 0x2a70})
        self.assertEqual(selectors.count(0x2a7a), 32)

        pic = (Path(assets) / 'GATE_301.PIC').read_bytes()
        entry = pic[5 * 12:6 * 12]
        palette_at = int.from_bytes(entry[:4], 'little') + 4 * entry[4]
        self.assertEqual(entry[5] & 0x1f, 0x13)
        self.assertEqual(palette_at, 0x25d27)
        self.assertEqual(ram[0x2b77c:0x2ba7c],
                         pic[palette_at:palette_at + 768])

    def test_byte_sparse_startup_with_original_file_restoration(self):
        executable, snapshot, assets = self.inputs()
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            profile = directory / 'byte-reads.txt'
            original = directory / 'original.ppm'
            minimal = directory / 'remaining-guest-state.bin'
            restored_final = directory / 'restored.ppm'
            restored_record = directory / 'restored.gvr'
            subprocess.run([executable, snapshot, assets, str(original),
                            '--full', '--read-deps', str(profile)],
                           check=True, capture_output=True, text=True)

            captured = Path(snapshot).read_bytes()
            guest = bytearray(len(captured))
            for row in profile.read_text().splitlines():
                if not row.startswith('range '):
                    continue
                _, a, b = row.split()
                start, end = int(a, 16), int(b, 16)
                if 0x2a000 <= start < 0x2b000:
                    continue  # The previously proven irrelevant page.
                guest[start:end] = captured[start:end]

            guest[0x2dd9d:0x2f48a] = bytes(5869)
            rtl = (Path(assets) / 'GATE.RTL').read_bytes()[0xd7bf:0xdabf]
            for index, value in enumerate(rtl):
                if value:
                    guest[0x1b77c + index] = 0
            for start, end in RTL_RESIDENT_RANGES:
                guest[start:end] = bytes(end - start)
            # Source the pinned 0x3105 palette from PIC entry #5 instead
            # of retaining the DOS loader's previous 768-byte RGB buffer.
            guest[0x2b77c:0x2ba7c] = bytes(768)
            # Replace the 105 FAR-selector WORDs with a derived native
            # runtime table based on the current DS segment (2a7a).
            guest[0x2f520:0x2f5f2] = bytes(210)

            # Defaults for the resident resource-extension pointers, mode,
            # executable-stem buffer and VGA status registers are recreated
            # by a separate native step. The loader's temporary four-byte
            # prefix header, DOS flag and image metadata are first-read but
            # independently ablated without affecting this recorded path.
            for start, end in (
                (0x2b756, 0x2b75a), (0x2b75e, 0x2b762),
                (0x2b76e, 0x2b770), (0x2bd6c, 0x2bd6e),
                (0x2d190, 0x2d194), (0x2dd2e, 0x2dd37),
                (0x2dd9c, 0x2dd9d), (0x2fc62, 0x2fc66),
                (0x2fcb4, 0x2fcb8),
            ):
                guest[start:end] = bytes(end - start)
            self.assertEqual(sum(value != 0 for value in guest), 22)
            minimal.write_bytes(guest)  # Private, never distributed.

            subprocess.run([executable, str(minimal), assets,
                            str(restored_final), '--full',
                            '--restore-exe-data', '--restore-rtl-data',
                            '--restore-palette', '--restore-selectors',
                            '--restore-defaults',
                            '--record', str(restored_record)],
                           check=True, capture_output=True, text=True)
            self.assertEqual(digest(restored_final),
                             '6a3f6394fbaac476700dbb54d30a8dd0f9028183493cfa3f416d9e4a7c87a777')
            self.assertEqual(digest(restored_record),
                             '856737f003f9af3a1ce4fdd3f7e494ee22b4cb7b9db36c728a6c9d57ad088ccf')

    def test_static_bootstrap_rejects_missing_and_incompatible_exe(self):
        executable, snapshot, _ = self.inputs()
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            ppm = directory / 'should-not-exist.ppm'
            for file_data in (None, b'NZ' + bytes(22966), b'MZ'):
                exe = directory / 'GATE.EXE'
                if exe.exists():
                    exe.unlink()
                if file_data is not None:
                    exe.write_bytes(file_data)  # Synthetic, no original data.
                proc = subprocess.run(
                    [executable, snapshot, str(directory), str(ppm),
                     '--full', '--restore-exe-data'],
                    capture_output=True, text=True)
                self.assertEqual(proc.returncode, 1)
                self.assertIn('cannot restore verified GATE.EXE', proc.stderr)
                self.assertFalse(ppm.exists())

    def test_static_bootstrap_rejects_missing_and_wrong_size_rtl(self):
        executable, snapshot, _ = self.inputs()
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            ppm = directory / 'should-not-exist.ppm'
            for file_data in (None, bytes(1024)):
                rtl = directory / 'GATE.RTL'
                if rtl.exists():
                    rtl.unlink()
                if file_data is not None:
                    rtl.write_bytes(file_data)  # Synthetic, no original data.
                proc = subprocess.run(
                    [executable, snapshot, str(directory), str(ppm),
                     '--full', '--restore-rtl-data'],
                    capture_output=True, text=True)
                self.assertEqual(proc.returncode, 1)
                self.assertIn('cannot restore verified GATE.RTL', proc.stderr)
                self.assertFalse(ppm.exists())

    def test_initial_palette_rejects_missing_and_invalid_picture(self):
        executable, snapshot, _ = self.inputs()
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            ppm = directory / 'should-not-exist.ppm'
            for file_data in (None, bytes(200)):
                pic = directory / 'GATE_301.PIC'
                if pic.exists():
                    pic.unlink()
                if file_data is not None:
                    pic.write_bytes(file_data)  # Synthetic, no original data.
                proc = subprocess.run(
                    [executable, snapshot, str(directory), str(ppm),
                     '--full', '--restore-palette'],
                    capture_output=True, text=True)
                self.assertEqual(proc.returncode, 1)
                self.assertIn('cannot restore original GATE_301.PIC', proc.stderr)
                self.assertFalse(ppm.exists())


if __name__ == '__main__':
    EXECUTABLE = sys.argv[1] if len(sys.argv) == 2 else None
    unittest.main(argv=[sys.argv[0]])
