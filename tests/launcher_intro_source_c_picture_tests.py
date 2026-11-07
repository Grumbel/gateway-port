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
    def original_inputs(self):
        if EXECUTABLE is None:
            self.skipTest('standalone executable path not supplied')
        snapshot = ROOT / 'build/intro-analysis/at-22000.bin'
        assets = ROOT.parent / 'gateway-dos'
        if not snapshot.is_file() or not (assets / 'GATE_300.PIC').is_file():
            self.skipTest('private DOS RAM snapshot or original PIC absent')
        return str(EXECUTABLE), str(snapshot), str(assets)

    def test_original_image(self):
        executable, snapshot, assets = self.original_inputs()
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'image.pgm'
            result = subprocess.run([executable, snapshot, assets, str(output)],
                                    check=True, capture_output=True, text=True)
            self.assertIn('resource 0x3001 mode 1: 2589 nonzero VGA pixels', result.stdout)
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

    def test_intro_resource_ids(self):
        executable, snapshot, assets = self.original_inputs()
        # These are original EXE:1302 display-call arguments. All IDs are
        # tested on a *fresh* snapshot, not concatenated into a fake intro.
        expected = {
            (0x3000, 1): (2394, '3dad20661269bef1bbab54d82c8068bbf4102b813ca5c0de118cac9c3037c012'),
            (0x301b, 1): (7574, '2b023f22c4044e9bd1569e886ffa26282468f579b52745528125754ed1670a9a'),
            (0x3105, 0): (64000, 'c5e49ba9c383123eb2a91ba5ca5e4c872076e3becbe0a18d3ea05df5dc9c88d8'),
            (0x3100, 0): (62772, 'ad6bd1f3118e1b29090989e05b03e5a5206f5766f52b4049843459c20c30c6cd'),
            (0x3102, 0): (61988, 'b9d2360e323c4a5b68dbcda4afb563d5e517b34de17d30bd1dd0d05271d4acee'),
            (0x3221, 0): (40880, '44388c91fbdea037f0b967f89ae8c44f10dcefad4ed2210d74be27823fdb72a2'),
            (0x3104, 0): (4418, 'fa886b38dde7586d2c1c8066dc154fe22ab497cdfaa5194f45b82cbc937309ec'),
            (0x332a, 0): (992, 'e6608228bb702c164dd207cd93ff00b1b2c94bb2c71cf806df89e728b7350623'),
            (0x8101, 1): (61105, '42cc7cfd26d791c7bbd94167723bc5469ea6744243cd485aa64fe81dfc477e79'),
            (0x8100, 1): (62772, 'ad6bd1f3118e1b29090989e05b03e5a5206f5766f52b4049843459c20c30c6cd'),
            (0x8102, 1): (61988, 'b9d2360e323c4a5b68dbcda4afb563d5e517b34de17d30bd1dd0d05271d4acee'),
        }
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'frame.pgm'
            for (resource, mode), (pixels, digest) in expected.items():
                with self.subTest(resource=hex(resource), mode=mode):
                    result = subprocess.run([executable, snapshot, assets,
                                             str(output), '--resource',
                                             hex(resource), str(mode)],
                                            check=True, capture_output=True,
                                            text=True)
                    self.assertIn(f'{pixels} nonzero VGA pixels', result.stdout)
                    self.assertRegex(result.stdout,
                                     r'\b(?:0|768) DAC component writes\b')
                    self.assertEqual(hashlib.sha256(output.read_bytes()).hexdigest(),
                                     digest)

    def test_original_rgb_dac(self):
        executable, snapshot, assets = self.original_inputs()
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'frame.ppm'
            subprocess.run([executable, snapshot, assets, str(output),
                            '--resource', '0x3105', '0', '--rgb'], check=True,
                           capture_output=True)
            frame = output.read_bytes()
            self.assertTrue(frame.startswith(b'P6\n320 200\n255\n'))
            self.assertEqual(hashlib.sha256(frame).hexdigest(),
                             'ea177bdef50098ced01d716c9e14d3e7c2987ece312bda44312c22a91a67ff43')

    def test_persistent_image_call_sequence(self):
        executable, snapshot, assets = self.original_inputs()
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run([executable, snapshot, assets,
                                     directory, '--sequence'],
                                    check=True, capture_output=True, text=True)
            frames = sorted(Path(directory).glob('source-c-*.ppm'))
            self.assertEqual(len(frames), 13)
            self.assertEqual(len(result.stdout.splitlines()), 13)
            expected_hashes = {
                'source-c-00-8101.ppm': '40e34b98c5ebbe405915a56f33c295e9e3b85b2914c54b024d2e5cd05bb35cc3',
                'source-c-01-8100.ppm': '83a253023f8473487ac6cba801ed0977b784a4a0f0547e40a49acc1322150de0',
                'source-c-02-8102.ppm': 'd5aa115a1ff5548d31265dede9c40a5f515f477c21735128ae4760a1472755b5',
                'source-c-03-3000.ppm': 'a95d4cb55feeb7b3ef7c2bd289f32d1ce3105da4e91d71348eb1eaa6dc9adce2',
                'source-c-04-301b.ppm': 'edddb0cd4dcb9a2398cfffe2dbb350d1866f4b2b4cf15e282e75cb28c8b092af',
                'source-c-05-3105.ppm': 'ea177bdef50098ced01d716c9e14d3e7c2987ece312bda44312c22a91a67ff43',
                'source-c-06-3100.ppm': '83a253023f8473487ac6cba801ed0977b784a4a0f0547e40a49acc1322150de0',
                'source-c-07-3102.ppm': 'd5aa115a1ff5548d31265dede9c40a5f515f477c21735128ae4760a1472755b5',
                'source-c-08-3221.ppm': '92cd6ef778886b6c6bd62619449bb40b54d2f92226e0dc9ef9d7b47555b04005',
                'source-c-09-3221.ppm': '92cd6ef778886b6c6bd62619449bb40b54d2f92226e0dc9ef9d7b47555b04005',
                'source-c-10-3221.ppm': '92cd6ef778886b6c6bd62619449bb40b54d2f92226e0dc9ef9d7b47555b04005',
                'source-c-11-3104.ppm': '207452fa66b870b65d7883198084eb20715afbe4401696321c0e934ed9d74476',
                'source-c-12-332a.ppm': '9cae20e6693d0f17169981e07e998e50fbf4c897a9355ad23ecc4ac10429f71d',
            }
            self.assertEqual(set(expected_hashes), {frame.name for frame in frames})
            for frame in frames:
                with self.subTest(name=frame.name):
                    self.assertEqual(hashlib.sha256(frame.read_bytes()).hexdigest(),
                                     expected_hashes[frame.name])


if __name__ == '__main__':
    EXECUTABLE = sys.argv[1] if len(sys.argv) == 2 else None
    unittest.main(argv=[sys.argv[0]])
