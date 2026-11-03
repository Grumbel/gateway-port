# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Synthetic relation graphs reproduce the resident helper's path semantics."""
import pathlib
import json
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
from inspect_relation_paths import relation_path


class RelationPathTests(unittest.TestCase):
    def test_nested_and_non_reflexive(self):
        objects = [{'object': 1, 'relation_links': [2]},
                   {'object': 2, 'relation_links': [3]},
                   {'object': 3, 'relation_links': []}]
        self.assertEqual(relation_path(objects, 1, 3), [1, 2, 3])
        self.assertIsNone(relation_path(objects, 1, 1))

    def test_multiple_links_stop_at_zero(self):
        objects = [{'object': 1, 'relation_links': [2, 0, 3]},
                   {'object': 2, 'relation_links': []},
                   {'object': 3, 'relation_links': []}]
        self.assertIsNone(relation_path(objects, 1, 3))
        objects[0]['relation_links'] = [2, 3]
        self.assertEqual(relation_path(objects, 1, 3), [1, 3])

    def test_cycle_terminates_and_self_loop_counts(self):
        objects = [{'object': 1, 'relation_links': [2]},
                   {'object': 2, 'relation_links': [1]}]
        self.assertIsNone(relation_path(objects, 1, 3))
        self.assertEqual(relation_path(objects, 1, 1), [1, 2, 1])

    def test_invalid_or_missing_objects(self):
        with self.assertRaisesRegex(ValueError, 'unknown source'):
            relation_path([], 1, 2)
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            relation_path([{'object': 1}, {'object': 1}], 1, 2)
        self.assertIsNone(relation_path([{'object': 1, 'relation_links': [999]}], 1, 3))

    def test_command_line_prints_the_relation_witness(self):
        objects = [{'object': 0x123, 'name': 'book', 'relation_links': [0x124]},
                   {'object': 0x124, 'name': 'drawer', 'relation_links': [0xd3]}]
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / 'objects.json'
            source.write_text(json.dumps({'objects': objects}))
            result = subprocess.run([sys.executable,
                                     str(pathlib.Path(__file__).resolve().parents[1] / 'tools' /
                                         'inspect_relation_paths.py'),
                                     str(source), '--source', '0x123', '--target', '0xd3'],
                                    capture_output=True, text=True, check=True)
        self.assertEqual(result.stdout.strip(), '0x123 (book) -> 0x124 (drawer) -> 0xd3')


if __name__ == '__main__':
    unittest.main()
