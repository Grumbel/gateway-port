"""Original Gateway object-child linked-list inspection without game assets."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from inspect_child_chains import resolve_chains

TOOL = Path(__file__).resolve().parents[1] / 'tools' / 'inspect_child_chains.py'


def fixture():
    return [
        dict(object=0x124, type=2, name='Drawer', child_links=[0x123, 0, 0],
             location=0x125, child_slot=0, next_sibling=0),
        dict(object=0x123, type=2, name='Book', child_links=[0, 0, 0],
             location=0x124, child_slot=0, next_sibling=0x175),
        dict(object=0x175, type=2, name='Other object', child_links=[0, 0, 0],
             location=0x124, child_slot=0, next_sibling=0),
        dict(object=0xd3, type=2, name='Player', child_links=[0, 0, 0],
             location=0xee, child_slot=0, next_sibling=0),
    ]


class ChildChainInspectionTests(unittest.TestCase):
    def test_walks_beyond_indexed_head_to_second_sibling(self):
        output = resolve_chains(fixture(), 0x124)
        self.assertIn('0x124 Drawer (type 2)', output)
        self.assertIn('0x123 Book', output)
        self.assertIn('0x175 Other object', output)
        self.assertIn('slot 1:', output)
        self.assertIn('(empty)', output)
        self.assertNotIn('!= parent', output)

    def test_reports_inconsistent_links_without_ignoring_them(self):
        objects = fixture()
        objects[1]['location'] = 0xd3
        objects[1]['child_slot'] = 1
        output = resolve_chains(objects, 0x124)
        self.assertIn('location=0x0d3 != parent', output)
        self.assertIn('own_slot=1 != 0', output)

    def test_nested_containment_and_recursive_cycle_do_not_loop(self):
        objects = fixture()
        objects[1]['child_links'] = [0xd3]
        objects[1]['next_sibling'] = 0
        objects[3]['location'] = 0x123
        output = resolve_chains(objects, 0x124, recursive=True)
        self.assertIn('0x0d3 Player', output)
        self.assertIn('0x123 Book (type 2) child slots:', output)
        objects[3]['child_links'] = [0x124]
        output = resolve_chains(objects, 0x124, recursive=True)
        self.assertIn('(recursive containment cycle)', output)

    def test_rejects_broken_sibling_chain_and_unknown_objects(self):
        objects = fixture()
        objects[2]['next_sibling'] = 0x123
        with self.assertRaisesRegex(ValueError, 'cycle in sibling chain'):
            resolve_chains(objects, 0x124)
        objects[1]['next_sibling'] = 0x777
        with self.assertRaisesRegex(ValueError, 'dangling child'):
            resolve_chains(objects, 0x124)
        with self.assertRaisesRegex(ValueError, 'duplicate or invalid'):
            resolve_chains(objects + [objects[0]], 0x124)
        with self.assertRaisesRegex(ValueError, 'absent from exported state'):
            resolve_chains(objects, 0x700)

    def test_cli_reads_inspection_json(self):
        with tempfile.TemporaryDirectory() as directory:
            state = Path(directory) / 'objects.json'
            state.write_text(json.dumps(dict(objects=fixture())))
            command = [sys.executable, str(TOOL), str(state), '--container', '0x124']
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('0x123 Book', result.stdout)
            bad = subprocess.run(command + ['--max-depth', '100'],
                                 capture_output=True, text=True)
            self.assertEqual(bad.returncode, 2)
            self.assertIn('out of range', bad.stderr)


if __name__ == '__main__':
    unittest.main()
