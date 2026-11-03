#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Verify native CALL decoding without needing copyrighted game binaries."""

import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import rank_engine_services as rank  # noqa: E402
from decomp_status import new_row, write_tracker


class ServiceRankingTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.exe = bytearray(0xc000)
        self.exe[:2] = b'MZ'
        self.exe[8:10] = (2).to_bytes(2, 'little')
        self.ovl = bytearray(0x100)
        self.sections = {4: dict(id=4, linked_segment=0x3300,
                                 original_file_offset=0x20, file_bytes=0x80)}
        self.exe[0x100:0x105] = bytes([0x9a, 0x56, 0x01, 0x10, 0x00])
        resident = bytes([0x55, 0x8b, 0xec, 0x51, 0x52, 0x50, 0xa1, 0x2a,
                          0x53, 0x5a, 0x59, 0x5d, 0xcb, 0x90, 0x90, 0x90])
        self.exe[0x276:0x286] = resident
        self.ram = bytearray(1 << 20)
        self.ram[0x10256:0x10266] = resident
        self.ovl[0x20:0x23] = bytes([0xe8, 0x05, 0x00])  # -> ovl:0x28
        self.ovl[0x30:0x35] = bytes([0x9a, 0x30, 0x00, 0x24, 0x1c])
        self.exe[0xa10a:0xa114] = bytes([0xe8, 0x00, 0x00, 0xea, 0x08, 0x00,
                                          0x00, 0x33, 0x04, 0x00])
        self.ovl[0x40:0x44] = bytes([0xff, 0x16, 0x00, 0x20])
        self.exe_path = self.root / 'target.exe'
        self.ovl_path = self.root / 'target.ovl'
        self.rtl_path = self.root / 'target.rtl'
        self.ram_path = self.root / 'native.bin'
        self.tracker = self.root / 'functions.tsv'
        self.plan = self.root / 'plan.json'
        self.facts = self.root / 'facts.json'
        rows = []
        for file, offset, section in (('exe', 0x100, ''), ('ovl', 0x20, '4'),
                                      ('ovl', 0x30, '4'), ('ovl', 0x40, '4')):
            row = new_row(file, offset, section)
            row.update(kind='handler', handler=str(len(rows) + 1))
            rows.append(row)
        write_tracker(self.tracker, rows)
        self.save()

    def save(self):
        self.exe_path.write_bytes(self.exe)
        self.ovl_path.write_bytes(self.ovl)
        self.rtl_path.write_bytes(b'ignored external synthetic code')
        self.ram_path.write_bytes(self.ram)
        self.plan.write_text(json.dumps(dict(
            schema_version=1, fingerprinted_gateway=True,
            exe_sha256=hashlib.sha256(self.exe).hexdigest(),
            ovl_sha256=hashlib.sha256(self.ovl).hexdigest(),
            sections=list(self.sections.values()))))
        records = []
        for file, offset, section in (('exe', 0x100, None), ('ovl', 0x20, 4),
                                      ('ovl', 0x30, 4), ('ovl', 0x40, 4)):
            site = dict(file=file, offset=offset)
            if section is not None:
                site['section'] = section
            records.append(dict(identity=site, call_sites=[site]))
        self.facts.write_text(json.dumps(dict(schema_version=1, functions=records)))

    def test_native_direct_near_far_and_rtlink(self):
        report = rank.analyze(self.facts, self.plan, self.exe_path,
                              self.ovl_path, self.tracker, self.rtl_path, self.ram_path)
        self.assertEqual((report['call_sites'], report['unresolved_sites']), (4, 1))
        found = {(item['file'], item['offset']): item for item in report['callees']}
        self.assertEqual(found[('ovl', 0x28)]['handler_callers'], 2)
        self.assertEqual(found[('exe', 0x276)]['handler_callers'], 1)
        self.assertEqual(found[('ovl', 0x28)]['call_kinds'], ['near', 'rtlink'])

    def test_reject_changed_originals_and_invalid_sources(self):
        self.exe[0x100] = 0
        self.exe_path.write_bytes(self.exe)
        with self.assertRaisesRegex(rank.CallGraphError, 'does not match'):
            rank.analyze(self.facts, self.plan, self.exe_path, self.ovl_path,
                         self.tracker)
        self.save()
        invalid = dict(file='ovl', offset=0x30, section=3)
        report = json.loads(self.facts.read_text())
        report['functions'][2]['call_sites'] = [invalid]
        self.facts.write_text(json.dumps(report))
        with self.assertRaisesRegex(rank.CallGraphError, 'wrong image/bank'):
            rank.analyze(self.facts, self.plan, self.exe_path, self.ovl_path,
                         self.tracker)

    def test_relocate_entry_words_using_intact_interior_window(self):
        resident = bytes(range(0x80, 0xc0))
        self.exe[0x276:0x2b6] = resident
        self.ram[0x10256:0x10296] = b'\x00' * 8 + resident[8:]
        self.save()
        report = rank.analyze(self.facts, self.plan, self.exe_path,
                              self.ovl_path, self.tracker, self.rtl_path, self.ram_path)
        entry = next(item for item in report['callees']
                     if item['file'] == 'exe' and item['offset'] == 0x276)
        self.assertEqual(entry['handler_callers'], 1)
        self.assertEqual(entry['native_match_bytes'], 16)


if __name__ == '__main__':
    unittest.main()
