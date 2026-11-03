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

    def test_generated_action_label_does_not_count_as_manual_name(self):
        self.functions.write_text('file\toffset\tsection\tghidra_address\tbody_bytes\tname\tsource\n'
                                  'ovl\t0x00e942\t5\t4661:1572\t64\tgw_action_71\tUSER_DEFINED\n')
        tracker.seed(self.path, functions_path=self.functions)
        row = tracker.read_tracker(self.path)[0]
        self.assertEqual((row['size'], row['name'], row['state']), ('64', '', 'unknown'))

    def test_generated_candidate_label_is_not_a_manual_name(self):
        self.functions.write_text('file\toffset\tsection\tghidra_address\tbody_bytes\tname\tsource\n'
                                  'ovl\t0x0514b5\t38\tGW_RT_038::0042\t64\t'
                                  'gw_ovl_514b5_candidate\tUSER_DEFINED\n')
        tracker.seed(self.path, self.functions, self.inventory)
        self.assertEqual(tracker.read_tracker(self.path)[0]['name'], '')
        rows = tracker.read_tracker(self.path)
        rows[0].update(name='gw_ovl_514b5_candidate', state='named')
        tracker.write_tracker(self.path, rows)
        self.assertEqual(tracker.seed(self.path, self.functions), (0, 2))
        self.assertEqual(tracker.read_tracker(self.path)[0]['state'], 'unknown')

    def test_timer_scheduler_native_boundaries_override_ghidra_splits(self):
        header = 'file\toffset\tsection\tghidra_address\tbody_bytes\tname\tsource\n'
        self.functions.write_text(header + '\n'.join(
            f'exe\t0x{offset:06x}\t\t105e:{offset:04x}\t{size}\tFUN_A\tDEFAULT'
            for offset, size in ((0x3e9d, 118), (0x3f13, 12), (0x3f20, 101),
                                 (0x3f85, 29), (0x3fa5, 16), (0x3fb5, 13))) + '\n')
        self.assertEqual(tracker.seed(self.path, functions_path=self.functions), (3, 3))
        rows = {tracker.key_of(row): row for row in tracker.read_tracker(self.path)}
        self.assertEqual({key[1]: row['size'] for key, row in rows.items()},
                         {0x3e9c: '119', 0x3f13: '114', 0x3f85: '61'})
        self.assertEqual(tracker.seed(self.path, functions_path=self.functions), (0, 0))

    def test_recursive_relation_body_keeps_internal_loop_as_one_function(self):
        header = 'file\toffset\tsection\tghidra_address\tbody_bytes\tname\tsource\n'
        self.functions.write_text(header + '\n'.join(
            f'exe\t0x{offset:06x}\t\t105e:{offset:04x}\t{size}\tFUN_A\tDEFAULT'
            for offset, size in ((0x2603, 46), (0x2631, 63))) + '\n')
        self.assertEqual(tracker.seed(self.path, functions_path=self.functions), (1, 1))
        row = tracker.read_tracker(self.path)[0]
        self.assertEqual((row['offset'], row['size']), ('0x002603', '109'))
        self.assertEqual(tracker.seed(self.path, functions_path=self.functions), (0, 0))

    def test_mark_explicit_handler_boundary_and_reject_tail(self):
        self.seeded()
        report = self.root / 'decompile.json'
        report.write_text(json.dumps(dict(schema_version=1, targets=[
            dict(role='overlay-entry-candidate', origin='GATEMAIN.OVL:0x514b5',
                 function_entry='49d8:0035', status='ok')
        ])))
        self.assertEqual(tracker.mark_handler_boundaries(self.path, report, self.functions),
                         (1, 1))
        self.assertIn(tracker.HANDLER_BOUNDARY_NOTE, self.path.read_text())
        original = self.path.read_text()
        self.assertEqual(tracker.mark_handler_boundaries(self.path, report, self.functions),
                         (1, 0))
        self.assertEqual(self.path.read_text(), original)
        bad = json.loads(report.read_text())
        bad['targets'][0]['context'] = 'contains another function'
        report.write_text(json.dumps(bad))
        with self.assertRaisesRegex(tracker.TrackerError, 'not an independent'):
            tracker.mark_handler_boundaries(self.path, report, self.functions)
        self.assertEqual(self.path.read_text(), original)

    def test_resident_action_is_seeded_as_exe_action(self):
        self.actions.write_text(json.dumps(dict(schema_version=1, action_count=1,
                                                actions=[dict(action=0x5f, native_code=dict(
                                                    filename='GATEMAIN.EXE', file_offset=0x6669))])))
        self.functions.write_text('file\toffset\tsection\tghidra_address\tbody_bytes\tname\tsource\n'
                                  'exe\t0x006669\t\t105e:6069\t20\tgw_action_5f\tUSER_DEFINED\n')
        tracker.seed(self.path, functions_path=self.functions, actions_path=self.actions)
        row = tracker.read_tracker(self.path)[0]
        self.assertEqual((row['file'], row['offset'], row['section'], row['kind'],
                          row['action'], row['size'], row['name']),
                         ('exe', '0x006669', '', 'action', '0x5f', '20', ''))

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
