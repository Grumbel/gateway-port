"""Original Gateway object-child linked-list inspection without game assets."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from inspect_child_chains import audit_chains, resolve_chains

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


def first_room_snapshot():
    """Compact reproduction of the complete first-room child output supplied by user.

    Values are the reported heads and sibling order, not a substitute for
    retaining or validating their original proprietary memory dump.
    """
    def record(object_id, kind, name, parent=None, child_slot=0,
               next_sibling=0, heads=None):
        result = dict(object=object_id, type=kind, name=name,
                      child_links=(heads if heads is not None else
                                   [0, 0, 0] if kind in (2, 7) else []))
        if parent is not None:
            result.update(location=parent, child_slot=child_slot,
                          next_sibling=next_sibling)
        return result

    return [
        record(0xee, 1, 'Your Quarters', heads=[0x175]),
        record(0x175, 8, 'picture', 0xee, next_sibling=0x16f),
        record(0x16f, 8, 'plant', 0xee, next_sibling=0x150),
        record(0x150, 2, 'wall locker', 0xee, next_sibling=0x14f),
        record(0x14f, 2, 'bed', 0xee, next_sibling=0x14e),
        record(0x14e, 2, 'desk chair', 0xee, next_sibling=0x131),
        record(0x131, 2, 'PV commset', 0xee, next_sibling=0x125,
               heads=[0, 0, 0x170]),
        record(0x170, 8, 'PV slot', 0x131, child_slot=2),
        record(0x125, 2, 'desk', 0xee, next_sibling=0xd3,
               heads=[0, 0x126, 0x124]),
        record(0x126, 2, 'debit card', 0x125, child_slot=1),
        record(0x124, 2, 'drawer', 0x125, child_slot=2),
        record(0xd3, 7, 'you', 0xee, heads=[0x123, 0xd7, 0]),
        record(0x123, 2, 'book', 0xd3),
        record(0xd7, 2, 'boots', 0xd3, child_slot=1, next_sibling=0xd5),
        record(0xd5, 2, 'white badge', 0xd3, child_slot=1, next_sibling=0xd4),
        record(0xd4, 2, 'blue coverall', 0xd3, child_slot=1),
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
        self.assertEqual(output.count('0x123 Book (type 2)'), 1)
        objects[3]['child_links'] = [0x124]
        output = resolve_chains(objects, 0x124, recursive=True)
        self.assertIn('(recursive containment cycle)', output)

    def test_first_room_mixed_type_hierarchy_and_global_audit(self):
        objects = first_room_snapshot()
        view = resolve_chains(objects, 0xee, recursive=True)
        self.assertLess(view.index('0x175 picture'), view.index('0x16f plant'))
        self.assertLess(view.index('0x16f plant'), view.index('0x150 wall locker'))
        self.assertLess(view.index('0x131 PV commset'), view.index('0x125 desk'))
        self.assertIn('slot 2:\n        0x170 PV slot', view)
        self.assertIn('slot 0:\n        0x123 book', view)
        self.assertIn('0x0d7 boots', view)
        self.assertIn('0x0d5 white badge', view)
        self.assertIn('0x0d4 blue coverall', view)
        self.assertEqual(view.count('0x131 PV commset (type 2)'), 1)
        audit = audit_chains(objects)
        self.assertIn('Native object records: 16', audit)
        self.assertIn('Nonzero indexed child heads: 6', audit)
        self.assertIn('Child links traversed: 15 (15 distinct objects)', audit)
        self.assertIn('Consistency findings: 0', audit)
        self.assertIn('Unlinked records (not reached from any indexed head): 1', audit)
        self.assertIn('Ownership roots with referenced descendants: 1', audit)
        self.assertIn('Maximum decoded nesting depth: 2', audit)
        self.assertIn('0x0ee Your Quarters (type 1): 15', audit)
        unlinked = audit_chains(objects, list_unlinked=True, unlinked_type=1, limit=1)
        self.assertIn('0x0ee type=1 location=? nonzero-heads=1 Your Quarters', unlinked)
        self.assertNotIn('0x123 type=2', unlinked)

    def test_audit_reports_corrupt_parent_slots_cycles_and_shared_children(self):
        objects = first_room_snapshot()
        objects[-1]['location'] = 0xee
        objects[-1]['child_slot'] = 2
        result = audit_chains(objects)
        self.assertIn('location 0x0ee != parent 0x0d3', result)
        self.assertIn('child slot 2 != parent slot 1', result)
        objects = first_room_snapshot()
        objects[1]['next_sibling'] = 0x175
        self.assertIn('sibling cycle at 0x175', audit_chains(objects))
        objects[1]['next_sibling'] = 0x777
        self.assertIn('missing child 0x777', audit_chains(objects))
        objects = first_room_snapshot()
        objects[8]['child_links'][0] = 0x175
        self.assertIn('referenced by multiple heads/chains', audit_chains(objects))

    def test_audit_detects_nested_containment_cycles_separately_from_sibling_cycles(self):
        objects = first_room_snapshot()
        # Player already belongs to the room, which is reached through the
        # room's own type-1 indexed head. Point another player slot to the
        # room to create a parent-ownership cycle without a sibling cycle.
        player = next(obj for obj in objects if obj['object'] == 0xd3)
        player['child_links'][2] = 0xee
        report = audit_chains(objects)
        self.assertIn('nested containment cycle:', report)
        self.assertIn('0x0d3', report)
        self.assertIn('0x0ee', report)
        self.assertNotIn('sibling cycle at', report)
        with self.assertRaisesRegex(ValueError, 'native object type'):
            audit_chains(objects, list_unlinked=True, unlinked_type=9)
        with self.assertRaisesRegex(ValueError, 'listing limit'):
            audit_chains(objects, limit=735)

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
            audited = subprocess.run([sys.executable, str(TOOL), str(state), '--audit'],
                                     capture_output=True, text=True)
            self.assertEqual(audited.returncode, 0, audited.stderr)
            self.assertIn('Consistency findings:', audited.stdout)
            report = subprocess.run([sys.executable, str(TOOL), str(state), '--audit',
                                     '--list-unlinked', '--unlinked-type', '2', '--limit', '1'],
                                    capture_output=True, text=True)
            self.assertEqual(report.returncode, 0, report.stderr)
            self.assertIn('Unlinked object details (type 2)', report.stdout)
            invalid = subprocess.run([sys.executable, str(TOOL), str(state), '--audit',
                                      '--unlinked-type', '2'], capture_output=True, text=True)
            self.assertEqual(invalid.returncode, 2)
            self.assertIn('requires --list-unlinked', invalid.stderr)


if __name__ == '__main__':
    unittest.main()
