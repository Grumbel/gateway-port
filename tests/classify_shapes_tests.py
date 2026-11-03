#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Asset-independent checks for conservative function-shape classification."""

import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import classify_shapes as classifier  # noqa: E402
from decomp_status import TrackerError, new_row, read_tracker, write_tracker


class ShapeTests(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.root = Path(folder.name)
        self.tracker = self.root / 'functions.tsv'
        self.facts = self.root / 'facts.json'
        self.rows = []
        self.items = []
        for offset, kind in ((0x10, 'handler'), (0x20, 'action'), (0x30, 'callback')):
            row = new_row('ovl', offset, '4')
            row['kind'] = kind
            row[kind] = '1'
            self.rows.append(row)
            self.items.append(dict(identity=dict(file='ovl', offset=offset, section=4),
                                   instruction_count=30, has_loop=False,
                                   indexed_memory=False, computed_calls=0,
                                   computed_jumps=0, direct_memory=[]))
        self.save()

    def save(self):
        write_tracker(self.tracker, self.rows)
        self.facts.write_text(json.dumps(dict(schema_version=1, functions=self.items)))

    def test_classifies_formulaic_direct_and_complex(self):
        self.items[1]['direct_memory'] = [dict(segment='DS', offset=0x8db4)]
        self.items[2]['has_loop'] = True
        self.save()
        changed, counts, share = classifier.classify(self.tracker, self.facts, write=True)
        self.assertEqual((changed, counts['formulaic'], counts['direct-memory'], share),
                         (3, 1, 1, 0.5))
        self.assertEqual([row['shape'] for row in read_tracker(self.tracker)],
                         ['formulaic', 'direct-memory', 'complex'])
        self.assertEqual(classifier.classify(self.tracker, self.facts, write=True)[0], 0)

    def test_conservative_heuristics(self):
        base = dict(self.items[0])
        for field, value in (('has_loop', True), ('indexed_memory', True),
                             ('computed_calls', 1), ('computed_jumps', 2),
                             ('instruction_count', 401)):
            with self.subTest(field=field):
                item = dict(base, **{field: value})
                self.assertEqual(classifier.measure(item), 'complex')
        self.assertEqual(classifier.measure(dict(base, computed_jumps=1)), 'formulaic')

    def test_missing_facts_and_manual_conflicts_do_not_mutate_tracker(self):
        self.items.pop(2)
        self.save()
        before = self.tracker.read_bytes()
        with self.assertRaisesRegex(TrackerError, 'missing Ghidra'):
            classifier.classify(self.tracker, self.facts, write=True)
        self.assertEqual(self.tracker.read_bytes(), before)
        self.items.append(dict(identity=dict(file='ovl', offset=0x30, section=4),
                               instruction_count=30, has_loop=False,
                               indexed_memory=False, computed_calls=0,
                               computed_jumps=0, direct_memory=[]))
        self.rows[0]['shape'] = 'complex'
        self.save()
        with self.assertRaisesRegex(TrackerError, 'conflicts'):
            classifier.classify(self.tracker, self.facts, write=True)


if __name__ == '__main__':
    unittest.main()
