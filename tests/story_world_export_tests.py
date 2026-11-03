# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Snapshot-to-world export: ordered mixed-type ownership without DOS assets."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from export_story_world import dot_room_candidates, export_world, markdown_world
from child_chain_inspect_tests import first_room_snapshot

TOOL = ROOT / 'tools' / 'export_story_world.py'


class StoryWorldExportTests(unittest.TestCase):
    def test_original_mixed_type_room_tree_and_inventory(self):
        world = export_world(first_room_snapshot())
        self.assertEqual(world['schema_version'], 2)
        self.assertEqual(world['counts']['records'], 16)
        self.assertEqual(world['counts']['indexed_children'], 15)
        self.assertEqual(world['counts']['roots'], 1)
        self.assertEqual(world['counts']['room_candidates'], 1)
        self.assertEqual(world['counts']['populated_roots'], 1)
        self.assertEqual(world['counts']['empty_room_candidates'], 0)
        self.assertEqual(world['counts']['rooms_with_direct_type8'], 1)
        self.assertEqual(world['counts']['direct_type8_room_children'], 2)
        self.assertEqual(world['counts']['max_depth'], 2)
        self.assertEqual(world['room_candidate_ids'], [0xee])
        room = world['roots'][0]
        self.assertEqual(room['name'], 'Your Quarters')
        self.assertEqual(room['descendant_count'], 15)
        children = room['child_slots'][0]['objects']
        self.assertEqual([entry['id'] for entry in children],
                         [0x175, 0x16f, 0x150, 0x14f, 0x14e, 0x131, 0x125, 0xd3])
        self.assertEqual(children[0]['native_type'], 8)
        self.assertEqual(children[-1]['native_type'], 7)
        pv = next(entry for entry in children if entry['id'] == 0x131)
        self.assertEqual(pv['child_slots'][2]['objects'][0]['id'], 0x170)
        desk = next(entry for entry in children if entry['id'] == 0x125)
        self.assertEqual(desk['child_slots'][1]['objects'][0]['id'], 0x126)
        self.assertEqual(desk['child_slots'][2]['objects'][0]['id'], 0x124)
        player = children[-1]
        self.assertEqual(player['child_slots'][0]['objects'][0]['id'], 0x123)
        self.assertEqual([entry['id'] for entry in player['child_slots'][1]['objects']],
                         [0xd7, 0xd5, 0xd4])
        self.assertNotIn('raw_state', str(world))
        report = markdown_world(world)
        self.assertIn('| `0x0ee` | Your Quarters | 8 | 2 | 15 |', report)
        self.assertIn('Type-1 room candidates', report)

    def test_original_type_six_relation_words_are_resolved_without_inventing_exits(self):
        objects = [dict(object=0x12, type=1, name='Satellite', child_links=[0]),
                   dict(object=0x13, type=1, name='Travel Pod', child_links=[0]),
                   dict(object=0x20, type=6, name='door', location=0x13,
                        relation_count=2, relation_links=[0x13, 0x12],
                        child_links=[0, 0])]
        world = export_world(objects)
        self.assertEqual(world['counts']['type6_connector_candidates'], 1)
        self.assertEqual(world['counts']['two_room_relation_candidates'], 1)
        connector = world['connector_candidates'][0]
        self.assertEqual(connector['id'], 0x20)
        self.assertTrue(connector['relations_available'])
        self.assertEqual(connector['native_location_reference'],
                         dict(id=0x13, name='Travel Pod', native_type=1))
        self.assertEqual([entry['name'] for entry in connector['relations']],
                         ['Travel Pod', 'Satellite'])
        self.assertEqual(connector['two_room_candidate'], [0x13, 0x12])
        door = next(root for root in world['roots'] if root['id'] == 0x20)
        self.assertEqual(door['native_relation_count'], 2)
        self.assertEqual(door['native_relation_links'], [0x13, 0x12])
        report = markdown_world(world)
        self.assertIn('Type-6 connector / multi-relation candidates', report)
        self.assertIn('Direct location word', report)
        self.assertIn('Travel Pod (type 1)', report)
        self.assertIn('Satellite (type 1)', report)
        self.assertIn('not confirmed exits', report)

        objects[2]['relation_links'] = [0x12, 0x13]
        with self.assertRaisesRegex(ValueError, 'first relation differs'):
            export_world(objects)
        objects[2]['relation_links'] = [0x13, 0x20]
        self.assertEqual(export_world(objects)['connector_candidates'][0]['two_room_candidate'], [])
        objects[2]['relation_links'] = [0x13, 0]
        self.assertEqual(export_world(objects)['connector_candidates'][0]['two_room_candidate'], [])
        objects[2]['relation_links'] = [0x13, 0x777]
        unresolved = export_world(objects)['connector_candidates'][0]
        self.assertNotIn('native_type', unresolved['relations'][1])
        self.assertEqual(unresolved['two_room_candidate'], [])
        objects[2]['relation_count'] = 3
        with self.assertRaisesRegex(ValueError, 'relation count mismatch'):
            export_world(objects)
        objects[2]['relation_count'] = 2
        del objects[2]['relation_links']
        incomplete = export_world(objects)
        self.assertFalse(incomplete['connector_candidates'][0]['relations_available'])
        self.assertIn('regenerate from inspect_object_state.py', markdown_world(incomplete))

    def test_dot_graph_has_only_two_room_candidates_and_escapes_names(self):
        objects = [dict(object=0x12, type=1, name='Satellite "A"', child_links=[0]),
                   dict(object=0x13, type=1, name='Travel Pod', child_links=[0]),
                   dict(object=0x20, type=6, name='door', location=0x13,
                        relation_count=2, relation_links=[0x13, 0x12]),
                   dict(object=0x21, type=6, name='disabled door', location=0,
                        relation_count=2, relation_links=[0, 0])]
        graph = dot_room_candidates(export_world(objects))
        self.assertIn('graph gateway_room_relation_candidates', graph)
        self.assertIn('room_012 [label="0x012 Satellite \\"A\\""];', graph)
        self.assertIn('room_013 -- room_012 [style=dashed, label="0x020 door"]', graph)
        self.assertNotIn('room_021', graph)
        self.assertNotIn('disabled door', graph)
        objects[2]['relation_links'] = [0x13, 0]
        self.assertNotIn('room_013', dot_room_candidates(export_world(objects)))

        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'objects.json'
            source.write_text(json.dumps({'objects': objects}))
            output = subprocess.run([sys.executable, str(TOOL), str(source),
                                     '--format', 'dot'], text=True, capture_output=True)
            self.assertEqual(output.returncode, 0, output.stderr)
            self.assertIn('graph gateway_room_relation_candidates', output.stdout)

    def test_detached_records_and_native_handler_metadata_survive(self):
        objects = first_room_snapshot()
        objects[1]['handler'] = 471
        objects[1]['native_code'] = dict(section=4, filename='GATEMAIN.OVL',
                                         file_offset=0xc879)
        objects.append(dict(object=0xbb, type=2, name='dike leak',
                            location=0x95, child_slot=0, next_sibling=0,
                            child_links=[0, 0, 0]))
        world = export_world(objects)
        self.assertEqual(world['counts']['roots'], 2)
        self.assertEqual(world['counts']['records'], 17)
        self.assertEqual(world['counts']['indexed_children'], 15)
        self.assertEqual(world['unattached_with_location'], [
            dict(id=0xbb, name='dike leak', native_type=2, native_location=0x95)])
        self.assertEqual(world['roots'][1]['id'], 0xee)
        picture = world['roots'][1]['child_slots'][0]['objects'][0]
        self.assertEqual(picture['native_handler'], 471)
        self.assertEqual(picture['native_code']['file_offset'], 0xc879)
        self.assertIn('dike leak', markdown_world(world))

    def test_repeated_type_one_head_alias_does_not_duplicate_children(self):
        objects = first_room_snapshot()
        objects[0]['child_links'] = [0x175, 0x175]
        world = export_world(objects)
        self.assertEqual(world['counts']['indexed_children'], 15)
        self.assertEqual(world['counts']['max_depth'], 2)
        slots = world['roots'][0]['child_slots']
        self.assertEqual(slots[1]['alias_of'], 0)
        self.assertEqual(slots[1]['objects'], [])

    def test_invalid_or_ambiguous_chains_fail_instead_of_becoming_world_data(self):
        objects = first_room_snapshot()
        with self.assertRaisesRegex(ValueError, 'duplicate or invalid'):
            export_world(objects + [objects[0]])
        objects = first_room_snapshot()
        objects[0]['child_links'][0] = 0x999
        with self.assertRaisesRegex(ValueError, 'dangling child'):
            export_world(objects)
        objects = first_room_snapshot()
        objects[1]['next_sibling'] = 0x175
        with self.assertRaisesRegex(ValueError, 'sibling cycle'):
            export_world(objects)
        objects = first_room_snapshot()
        objects[1]['location'] = 0xd3
        with self.assertRaisesRegex(ValueError, 'location differs'):
            export_world(objects)
        objects = first_room_snapshot()
        del objects[1]['next_sibling']
        with self.assertRaisesRegex(ValueError, 'sibling accessor not decoded'):
            export_world(objects)
        objects = first_room_snapshot()
        objects[8]['child_links'][0] = 0x175
        with self.assertRaisesRegex(ValueError, 'multiple indexed owners'):
            export_world(objects)
        objects = first_room_snapshot()
        player = next(obj for obj in objects if obj['object'] == 0xd3)
        player['child_links'][2] = 0xee
        room = objects[0]
        room['location'] = 0xd3
        room['child_slot'] = 2
        room['next_sibling'] = 0
        with self.assertRaisesRegex(ValueError, 'no containment root'):
            export_world(objects)

    def test_cli_exports_machine_readable_json_or_human_room_inventory(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'objects.json'
            source.write_text(json.dumps({'objects': first_room_snapshot()}))
            command = [sys.executable, str(TOOL), str(source)]
            report = subprocess.run(command, text=True, capture_output=True)
            self.assertEqual(report.returncode, 0, report.stderr)
            self.assertIn('| `0x0ee` | Your Quarters | 8 | 2 | 15 |', report.stdout)
            structured = subprocess.run(command + ['--format', 'json'],
                                        text=True, capture_output=True)
            self.assertEqual(structured.returncode, 0, structured.stderr)
            self.assertEqual(json.loads(structured.stdout)['counts']['indexed_children'], 15)
            source.write_text(json.dumps({'objects': [{'object': 1, 'type': 1,
                                                       'child_links': [123]}]}))
            broken = subprocess.run(command, text=True, capture_output=True)
            self.assertEqual(broken.returncode, 2)
            self.assertIn('dangling child', broken.stderr)


if __name__ == '__main__':
    unittest.main()
