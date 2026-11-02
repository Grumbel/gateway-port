"""No Gateway binaries required: overlay-address collisions and census validation."""

import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from inventory_native_handlers import (format_text, inventory, map_entry,
                                       parse_calls, prefix_clusters)


class HandlerInventoryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.plan = dict(schema_version=1, sections=[])
        for sid in (7, 8):
            image = root / f'section-{sid}.bin'
            # Same virtual base and same 32-byte prefix are not evidence of
            # the same semantic routine or even the same loaded overlay.
            data = bytes.fromhex('558bec') + bytes(93)
            image.write_bytes(data)
            self.plan['sections'].append(dict(id=sid, runtime_segment=0x4000,
                file_bytes=len(data), original_file_offset=sid * 1000,
                relocated_file=str(image), relocated_sha256=hashlib.sha256(data).hexdigest()))
        self.objects = dict(object_count=3, handler_count=2, objects=[
            self.obj(1, 12, 7), self.obj(2, 12, 7), self.obj(3, 13, 8)])
        self.callbacks = dict(schema_version=1, table_count=44, callbacks=[
            dict(callback_id=4, uses=[{}, {}], status='rtlink',
                 rtlink_reference=dict(section=8, target_segment=0x4000,
                                       target_offset=16))])
        self.lines = ['rtlink-census manager_hits=7 validated=6 unrecognized=1',
                      'rtlink-call section=7 target=4000:10 count=2 first_ms=10 last_ms=30',
                      'rtlink-call section=8 target=4000:10 count=3 first_ms=13 last_ms=40',
                      'rtlink-call section=7 target=4000:20 count=1 first_ms=20 last_ms=20']

    @staticmethod
    def obj(oid, hid, sid):
        return dict(object=oid, name=f'object {oid}', type=5, handler=hid,
                    rtlink_stub=dict(section_word=sid, target_segment=0x4000,
                                     target_offset=16))

    def test_section_identity_survives_same_linear_address(self):
        calls, header = parse_calls(self.lines)
        report = inventory(self.objects, self.plan, self.callbacks, calls, header)
        summary = report['summary']
        self.assertEqual((summary['object_count'], summary['objects_with_handlers'],
                          summary['distinct_handlers'], summary['reused_handler_ids']),
                         (3, 3, 2, 1))
        self.assertEqual(summary['observed_handlers'], 2)
        self.assertEqual(summary['observed_callbacks'], 1)
        self.assertEqual(summary['callback_entries_also_object_handlers'], 1)
        self.assertEqual(summary['object_handler_invocations'], 5)
        self.assertEqual(summary['unclassified_rtlink_invocations'], 1)
        self.assertEqual(report['handlers'][0]['trace']['calls'], 2)
        self.assertEqual(report['handlers'][1]['trace']['calls'], 3)
        self.assertEqual(report['unclassified_rtlink_targets'][0]['entry']['file_offset'], 7032)
        self.assertIn('Observed object entries: 2/2', format_text(report))

    def test_without_replay_is_unknown_not_zero_coverage(self):
        report = inventory(self.objects, self.plan)
        self.assertIsNone(report['summary']['observed_handlers'])
        self.assertIsNone(report['handlers'][0]['trace'])
        self.assertIn('not measured', format_text(report))

    def test_exact_prefix_clustering_does_not_merge_section_entries(self):
        report = inventory(self.objects, self.plan)
        clusters = prefix_clusters(report, self.plan)
        self.assertEqual([item['handler_ids'] for item in clusters], [[12, 13]])
        self.assertEqual(len(report['handlers']), 2)
        self.plan['sections'][1]['relocated_sha256'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'fingerprint mismatch'):
            prefix_clusters(report, self.plan)

    def test_corrupted_census_is_rejected_not_silently_accepted(self):
        for corrupt in (self.lines[:1],
                        [self.lines[0].replace('validated=6', 'validated=7'), *self.lines[1:]],
                        [self.lines[0], self.lines[1], self.lines[1], self.lines[2]],
                        [self.lines[0], self.lines[1].replace('first_ms=10', 'first_ms=31'),
                         *self.lines[2:]],
                        ['not a census']):
            with self.subTest(corrupt=corrupt), self.assertRaises(ValueError):
                parse_calls(corrupt)

    def test_duplicate_handler_conflicts_and_out_of_bounds_rejected(self):
        wrong = json.loads(json.dumps(self.objects))
        wrong['objects'][1]['rtlink_stub']['section_word'] = 8
        with self.assertRaisesRegex(ValueError, 'conflicting native entry'):
            inventory(wrong, self.plan)
        with self.assertRaisesRegex(ValueError, 'not file-backed'):
            map_entry((7, 0x4000, 200), {s['id']: s for s in self.plan['sections']})

    def test_duplicate_callback_id_rejected(self):
        callbacks = json.loads(json.dumps(self.callbacks))
        callbacks['callbacks'].append(callbacks['callbacks'][0])
        with self.assertRaisesRegex(ValueError, 'duplicate room callback'):
            inventory(self.objects, self.plan, callbacks)


if __name__ == '__main__':
    unittest.main()
