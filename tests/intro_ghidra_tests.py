#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Intro Ghidra preparation tests; no Ghidra download or game bytes committed."""

import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from prepare_intro_ghidra import (EXE_SHA256, RTL_SHA256, EXE_ENTRIES,
                                  RTL_BANKS, RAW_COPY_CANDIDATE,
                                  mz_relocations, prepare, staged_unlinked_copy)
from run_intro_ghidra import command, run


class IntroGhidraTests(unittest.TestCase):
    def test_missing_or_invalid_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            bad_exe = base / 'wrong.exe'
            bad_exe.write_bytes(b'MZ' + b'\x00' * 100)
            with self.assertRaisesRegex(ValueError, 'pinned original'):
                prepare(bad_exe, bad_exe, bad_exe, base / 'out')
            with self.assertRaisesRegex(ValueError, 'relocation directory'):
                mz_relocations(b'MZ' + b'\x00' * 100)
            with self.assertRaisesRegex(ValueError, 'name'):
                run(bad_exe, bad_exe, bad_exe, base, project_name='not-safe!')

    def test_headless_command(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            (base / 'intro-ghidra-plan.json').write_text(json.dumps({
                'exe_file': '/unused/GATE.EXE'}))
            result = command(['python3', '-m', 'pyghidra.ghidra_launch'],
                             base / 'intro-ghidra-plan.json', 'GatewayIntro')
            self.assertIn('ImportGatewayIntro.py', result)
            self.assertIn('DecompileGatewayIntro.py', result)
            self.assertEqual(result[result.index('-import') + 1], '/unused/GATE.EXE')
            self.assertIn('-preScript', result)
            self.assertIn('-postScript', result)


class RealGatewayIntroEvidence(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        original = ROOT.parent / 'gateway-dos'
        cls.exe = original / 'GATE.EXE'
        cls.rtl = original / 'GATE.RTL'
        cls.snapshot = ROOT / 'build/intro-analysis/at-22000.bin'
        for path, expected in ((cls.exe, EXE_SHA256), (cls.rtl, RTL_SHA256)):
            if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
                raise unittest.SkipTest('pinned original Gateway intro assets absent')
        if not cls.snapshot.is_file():
            raise unittest.SkipTest('private linked DOS guest RAM snapshot absent')

    def test_relocated_import_is_file_accurate(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            plan_file, plan = prepare(self.exe, self.rtl, self.snapshot, output)
            self.assertEqual(json.loads(plan_file.read_text()), plan)
            self.assertEqual(len(plan['sections']), len(RTL_BANKS))
            self.assertEqual({e['file_offset'] for e in plan['exe_labels']},
                             set(EXE_ENTRIES))
            self.assertGreaterEqual(sum(len(s['entries']) for s in plan['sections']), 44)
            self.assertTrue(any(e['name'] == 'gate_rtl_130ab'
                                for s in plan['sections'] for e in s['entries']))
            file_services = next(section for section in plan['sections']
                                 if section['segment'] == 0x0c33)
            self.assertEqual({item['file_offset'] for item in file_services['entries']},
                             {0xe4ff, 0xe511, 0xe531, 0xe582, 0xe59b,
                              0xe5c5, 0xe5e3, 0xe61e, 0xe637, 0xe650,
                              0xe5fc, 0xe669, 0xe68b})
            self.assertTrue(all(item['role'] == 'source-reconstructed'
                                for item in file_services['entries']))
            self.assertEqual(len(file_services['witnesses']), 4)
            snapshot = self.snapshot.read_bytes()
            for section in plan['sections']:
                with self.subTest(segment=section['segment']):
                    start = (plan['load_segment'] + section['segment']) * 16
                    image = snapshot[start:start + section['size']]
                    self.assertEqual(Path(section['relocated_file']).read_bytes(), image)
                    self.assertEqual(hashlib.sha256(image).hexdigest(),
                                     section['relocated_sha256'])
                    self.assertGreater(section['exact_byte_matches'],
                                       section['size'] * 0.60)
                    for entry in section['entries']:
                        self.assertEqual(entry['offset'],
                                         entry['file_offset'] - section['file_base'])
            self.assertIn(0x8298, plan['unmapped_rtl_entries'])
            self.assertEqual(plan['unlinked_staging_candidates'][0]['segment'],
                             RAW_COPY_CANDIDATE['segment'])
            self.assertNotIn(RAW_COPY_CANDIDATE['segment'],
                             {section['segment'] for section in plan['sections']})

    def test_staged_copy_cannot_be_imported_as_linked_code(self):
        ram = bytearray(self.snapshot.read_bytes())
        rtl = self.rtl.read_bytes()
        region = RAW_COPY_CANDIDATE
        start = (0x1000 + region['segment']) * 16
        base = region['file_base']
        self.assertEqual(ram[start:start + region['size']],
                         rtl[base:base + region['size']])
        self.assertEqual(rtl[base + 0x11:base + 0x15], b'\0' * 4)
        self.assertEqual(len(staged_unlinked_copy(rtl, ram, 0x1000)), 1)
        ram[start + 0x20] ^= 0xff
        self.assertEqual(staged_unlinked_copy(rtl, ram, 0x1000), [])

    def test_refuse_snapshot_not_matching_linked_bank(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            damaged = bytearray(self.snapshot.read_bytes())
            witness = RTL_BANKS[2]
            start = (0x1000 + witness['segment']) * 16
            damaged[start:start + witness['size']] = b'\x00' * witness['size']
            path = base / 'wrong.bin'
            path.write_bytes(damaged)
            with self.assertRaisesRegex(ValueError, 'does not contain RTL bank'):
                prepare(self.exe, self.rtl, path, base / 'out')

    def test_runner_refuses_stale_results(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            (base / 'intro-ghidra-report.json').write_text('{}')
            with self.assertRaisesRegex(ValueError, 'already exists'):
                run(self.exe, self.rtl, self.snapshot, base)


if __name__ == '__main__':
    unittest.main()
