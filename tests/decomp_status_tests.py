#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Tests for the native-code recovery tracker (tools/decomp_status.py)."""

import io
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import decomp_status as tracker  # noqa: E402


FUNCTIONS = '''file\toffset\tsection\tghidra_address\tbody_bytes\tname\tsource
exe\t0x0028e4\t\t1000:28c4\t58\tgw_test_object_flag\tUSER_DEFINED
exe\t0x003000\t\t1000:2fe0\t12\tFUN_1000_2fe0\tDEFAULT
ovl\t0x0514b5\t38\t49d8:0035\t32\tgw_object_0ee_your_quarters\tUSER_DEFINED
ovl\t0x060000\t40\t4661:0000\t90\tFUN_GW_RT_040__046610\tDEFAULT
ovl\t0x070000\t41\t4661:0000\t40\tgw_take_book\tUSER_DEFINED
'''

INVENTORY = dict(schema_version=1, handlers=[
    dict(handler=7, entry=dict(section=38, file_offset=0x514b5),
         objects=[dict(id=0xee, type=1, name='game text stays out')]),
], callbacks=[dict(callback_id=0x2a, entry=dict(section=40, file_offset=0x60000))],
    unclassified_rtlink_targets=[dict(entry=dict(section=41, file_offset=0x70000), calls=3)])

ACTIONS = dict(schema_version=1, action_count=2, actions=[
    dict(action=0x46, native_code=dict(filename='GATEMAIN.OVL', section=41,
                                       file_offset=0x70000)),
    dict(action=0x47, selector=1,
         native_code=dict(filename='GATEMAIN.OVL', section=42, file_offset=0x71000),
         selector_native_code=dict(filename='GATEMAIN.OVL', section=42,
                                   file_offset=0x72000)),
])


class TrackerTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.path = self.root / 'decomp/functions.tsv'
        self.functions = self.root / 'ghidra-functions.tsv'
        self.functions.write_text(FUNCTIONS)
        self.inventory = self.root / 'inventory.json'
        self.inventory.write_text(json.dumps(INVENTORY))
        self.actions = self.root / 'actions.json'
        self.actions.write_text(json.dumps(ACTIONS))

    def seeded(self):
        tracker.seed(self.path, self.functions, self.inventory, self.actions)
        return {tracker.key_of(row): row for row in tracker.read_tracker(self.path)}

    def test_seed_merges_sources(self):
        rows = self.seeded()
        self.assertEqual(len(rows), 7)
        handler = rows[('ovl', 0x514b5)]
        self.assertEqual((handler['kind'], handler['handler'], handler['objects'],
                          handler['section'], handler['size']),
                         ('handler', '7', '0x0ee', '38', '32'))
        # Generated object aliases embed game text: never copied as names.
        self.assertEqual(handler['name'], '')
        self.assertNotIn('game text', self.path.read_text())
        self.assertEqual(rows[('ovl', 0x60000)]['callback'], '0x2a')
        self.assertEqual(rows[('exe', 0x28e4)]['state'], 'named')
        self.assertEqual(rows[('exe', 0x3000)]['name'], '')

    def test_action_evidence_replaces_generic_service(self):
        rows = self.seeded()
        take = rows[('ovl', 0x70000)]
        self.assertEqual((take['kind'], take['action'], take['name']),
                         ('action', '0x46', 'gw_take_book'))
        selector = rows[('ovl', 0x72000)]
        self.assertEqual(selector['kind'], 'service')
        self.assertIn('0x47', selector['notes'])
        # No Ghidra function yet: still tracked, size unknown.
        self.assertEqual(rows[('ovl', 0x71000)]['size'], '')

    def test_seed_is_idempotent_and_keeps_manual_columns(self):
        self.seeded()
        rows = tracker.read_tracker(self.path)
        for row in rows:
            if row['offset'] == '0x0514b5':
                row.update(name='your_quarters_events', state='named', shape='formulaic',
                           notes='manual')
        tracker.write_tracker(self.path, rows)
        before = self.path.read_text()
        self.assertEqual(tracker.seed(self.path, self.functions, self.inventory,
                                      self.actions), (0, 0))
        self.assertEqual(self.path.read_text(), before)

    def test_seed_rejects_changed_identity(self):
        self.seeded()
        changed = json.loads(json.dumps(INVENTORY))
        changed['handlers'][0]['handler'] = 8
        self.inventory.write_text(json.dumps(changed))
        with self.assertRaisesRegex(tracker.TrackerError, 'handler changed'):
            tracker.seed(self.path, inventory_path=self.inventory)

    def test_check_accepts_seed_and_enforces_pipeline(self):
        self.seeded()
        rows = tracker.read_tracker(self.path)
        self.assertEqual(tracker.check(rows, self.root), [])
        row = next(row for row in rows if row['offset'] == '0x0514b5')
        row.update(name='your_quarters_events', state='ported')
        errors = tracker.check(rows, self.root)
        self.assertTrue(any('measured shape' in error for error in errors))
        self.assertTrue(any('requires module' in error for error in errors))
        row.update(shape='formulaic', module='src/story/your_quarters.cpp')
        (self.root / 'src/story').mkdir(parents=True)
        (self.root / 'src/story/your_quarters.cpp').write_text('// test')
        self.assertEqual(tracker.check(rows, self.root), [])
        row['state'] = 'verified'
        self.assertTrue(any('test path' in error for error in tracker.check(rows, self.root)))

    def test_check_rejects_bad_rows(self):
        self.seeded()
        rows = tracker.read_tracker(self.path)
        rows[0]['section'] = '3'
        rows[1]['state'] = 'done'
        rows[-1]['offset'] = '0x10'
        errors = tracker.check(rows, self.root)
        self.assertTrue(any('no RTLink section' in error for error in errors))
        self.assertTrue(any('unknown state' in error for error in errors))
        self.assertTrue(any('sorted' in error for error in errors))

    def test_next_prefers_started_then_services_then_small(self):
        rows = list(self.seeded().values())
        order = [(row['kind'], row['offset']) for row in tracker.propose(rows, limit=0)]
        self.assertEqual(order[0][0], 'service')
        for row in rows:
            if row['offset'] == '0x0514b5':
                row.update(name='your_quarters_events', state='analyzed', shape='formulaic')
        self.assertEqual(tracker.propose(rows, limit=1)[0]['offset'], '0x0514b5')
        self.assertTrue(all(row['kind'] == 'handler'
                            for row in tracker.propose(rows, kind='handler', limit=0)))

    def test_summary_runs(self):
        out = io.StringIO()
        tracker.summary(list(self.seeded().values()), out)
        self.assertIn('7 routines', out.getvalue())


class RepositoryTrackerTest(unittest.TestCase):
    def test_committed_tracker_is_valid(self):
        rows = tracker.read_tracker(tracker.DEFAULT_TRACKER)
        self.assertTrue(rows)
        self.assertEqual(tracker.check(rows), [])


if __name__ == '__main__':
    unittest.main()
