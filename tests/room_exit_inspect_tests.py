# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Gateway's four-byte native room exit table, without proprietary assets."""
from pathlib import Path
import json
import struct
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from inspect_room_exits import format_dot, format_stats, format_text, inspect_exits


TOOL = Path(__file__).resolve().parents[1] / 'tools' / 'inspect_room_exits.py'


def room(object_id, name, count, first):
    raw = bytearray(26)
    raw[0x16] = count
    struct.pack_into('<H', raw, 0x18, first)
    return dict(object=object_id, type=1, name=name, raw_state=raw.hex())


class RoomExitInspectTests(unittest.TestCase):
    def setUp(self):
        self.memory = bytearray(0xa0000)
        self.ds = 0x1000
        self.table_segment = 0x2400
        self.conditional_segment = 0x2500
        self.property_segment = 0x2600
        struct.pack_into('<H', self.memory, self.ds * 16 + 0x8dcc,
                         self.table_segment)
        struct.pack_into('<H', self.memory, self.ds * 16 + 0x8dd2,
                         self.conditional_segment)
        struct.pack_into('<H', self.memory, self.ds * 16 + 0x8d56,
                         self.property_segment)
        for property_id, bit in [(0x0c, 0), (0x10, 1)]:
            descriptor = self.property_segment * 16 + 0x14 + property_id * 4
            self.memory[descriptor] = bit
            struct.pack_into('<H', self.memory, descriptor + 2, 1)
        self.objects = [room(0xee, 'Your Quarters', 1, 1),
                        room(0xf1, 'Heecheetown', 2, 2),
                        room(0xf2, 'Inactive', 2, 0),
                        dict(object=0xd3, type=7, name='you')]
        self.entry(0, 0x11, 1, 0xf1)
        self.entry(1, 0x12, 1, 0xee)
        self.entry(2, 0x33, 2, 0x27)
        struct.pack_into('<HH', self.memory,
                         self.conditional_segment * 16 + 0x4996 + 4 * 0x27,
                         0xee, 0xf1)

    def entry(self, index, direction, kind, argument):
        struct.pack_into('<BBH', self.memory,
                         self.table_segment * 16 + 0x417e + index * 4,
                         direction, kind, argument)

    def test_room_fields_one_based_index_and_direct_destinations(self):
        snapshot = inspect_exits(self.memory, self.ds, self.objects)
        self.assertEqual(snapshot['table_segment'], self.table_segment)
        quarters, town, inactive = snapshot['rooms']
        self.assertEqual(quarters['count'], 1)
        self.assertEqual(quarters['first'], 1)
        self.assertEqual(quarters['exits'], [
            dict(index=0, direction_code=0x11, kind=1, kind_meaning='direct-room',
                 argument=0xf1, direct_room=0xf1, direct_room_name='Heecheetown')])
        self.assertEqual([route['index'] for route in town['exits']], [1, 2])
        self.assertNotIn('direct_room', town['exits'][1])
        self.assertEqual(town['exits'][1]['kind_meaning'], 'conditional')
        self.assertEqual(town['exits'][1]['conditional_destination'], 0xee)
        self.assertEqual(town['exits'][1]['conditional_room'], 0xee)
        self.assertEqual(town['exits'][1]['condition_object'], 0xf1)
        self.assertEqual(town['exits'][1]['condition_name'], 'Heecheetown')
        self.assertEqual(snapshot['conditional_table_segment'], self.conditional_segment)
        self.assertFalse(inactive['active'])
        self.assertEqual(inactive['exits'], [])
        report = format_text(snapshot)
        self.assertIn('0x0ee Your Quarters: count=1 first=1', report)
        self.assertIn('direction=0x11 kind=1 (direct-room) argument=0x0f1', report)
        self.assertIn('native dispatcher skips', report)
        self.assertIn('conditional_destination=0x0ee condition_object=0x0f1', report)
        self.assertIn('not verified reachable', report)
        stats = format_stats(snapshot)
        self.assertIn('Native type-1 rooms: 3 (2 with indexed exits)', stats)
        self.assertIn('Exit kinds: 1=2, 2=1', stats)
        self.assertIn('Kind-2 resolved room targets: 1 of 1', stats)

    def test_dot_uses_only_direct_edges_and_escapes_names(self):
        self.objects[1]['name'] = 'Heecheetown "A"'
        graph = format_dot(inspect_exits(self.memory, self.ds, self.objects))
        self.assertIn('digraph gateway_native_exits', graph)
        self.assertIn('n0ee -> n0f1 [label="0x11"]', graph)
        self.assertIn('n0f1 -> n0ee [label="0x12"]', graph)
        self.assertIn('Heecheetown \\"A\\"', graph)
        self.assertNotIn('0x33', graph)  # conditional branch is not a direct room edge
        with_condition = format_dot(inspect_exits(self.memory, self.ds, self.objects),
                                    include_conditional=True)
        self.assertIn('n0f1 -> n0ee [label="0x33", style=dashed', with_condition)
        self.assertIn('tooltip="object 0x0f1: flags 0x0c/0x10"', with_condition)

    def test_preserves_unknown_kinds_and_unresolved_direct_targets(self):
        self.entry(0, 0xaa, 9, 0xf1)
        self.entry(1, 0xbb, 1, 0x999)
        world = inspect_exits(self.memory, self.ds, self.objects)
        self.assertEqual(world['rooms'][0]['exits'][0]['kind_meaning'], 'unknown')
        self.assertNotIn('direct_room', world['rooms'][1]['exits'][0])
        self.assertNotIn('n0ee', format_dot(world))

    def test_conditional_only_room_is_visible_in_optional_graph(self):
        self.entry(0, 0x03, 2, 0x0e)
        struct.pack_into('<HH', self.memory,
                         self.conditional_segment * 16 + 0x4996 + 4 * 0x0e,
                         0xf1, 0xee)
        world = inspect_exits(self.memory, self.ds, self.objects)
        self.assertNotIn('n0ee -> n0f1', format_dot(world))
        optional = format_dot(world, include_conditional=True)
        self.assertIn('n0ee -> n0f1 [label="0x03", style=dashed', optional)
        self.assertEqual(world['rooms'][0]['exits'][0]['condition_object'], 0xee)
        self.assertEqual(world['rooms'][0]['exits'][0]['conditional_room'], 0xf1)

    def test_original_property_bits_in_final_native_snapshot(self):
        state = bytearray.fromhex(self.objects[1]['raw_state'])
        state[4] = 0x01  # flag 0x0c set, 0x10 clear
        self.objects[1]['raw_state'] = state.hex()
        world = inspect_exits(self.memory, self.ds, self.objects, condition_flags=True)
        condition = world['rooms'][1]['exits'][1]
        self.assertEqual(condition['snapshot_flags'], {'0x0c': True, '0x10': False})
        self.assertIn('snapshot_flags=0x0c:set,0x10:clear', format_text(world))
        self.assertTrue(world['snapshot_conditions'])
        self.assertNotIn('snapshot_flags', inspect_exits(
            self.memory, self.ds, self.objects)['rooms'][1]['exits'][1])
        # Type mask zero means unsupported, not an asserted clear bit.
        struct.pack_into('<H', self.memory,
                         self.property_segment * 16 + 0x14 + 0x10 * 4 + 2, 0)
        unsupported_world = inspect_exits(self.memory, self.ds, self.objects,
                                          condition_flags=True)
        unsupported = unsupported_world['rooms'][1]['exits'][1]
        self.assertIsNone(unsupported['snapshot_flags']['0x10'])
        self.assertIn('0x10:unsupported', format_text(unsupported_world))
        self.memory[self.property_segment * 16 + 0x14 + 0x0c * 4] = 0xff
        with self.assertRaisesRegex(ValueError, 'flag bit outside native record'):
            inspect_exits(self.memory, self.ds, self.objects, condition_flags=True)
        struct.pack_into('<H', self.memory, self.ds * 16 + 0x8d56, 0)
        with self.assertRaisesRegex(ValueError, 'no native property-table segment'):
            inspect_exits(self.memory, self.ds, self.objects, condition_flags=True)

    def test_snapshot_room_selection_and_structural_validation(self):
        selected = inspect_exits(self.memory, self.ds, self.objects, [0xee])
        self.assertEqual([room['id'] for room in selected['rooms']], [0xee])
        with self.assertRaisesRegex(ValueError, 'unknown room ID'):
            inspect_exits(self.memory, self.ds, self.objects, [0x999])
        with self.assertRaisesRegex(ValueError, 'not a type-1 room'):
            inspect_exits(self.memory, self.ds, self.objects, [0xd3])
        with self.assertRaisesRegex(ValueError, 'record too short'):
            inspect_exits(self.memory, self.ds, [room(0xee, '', 1, 1) |
                                                 {'raw_state': bytes(20).hex()}])
        with self.assertRaisesRegex(ValueError, 'exit table entry wraps segment'):
            inspect_exits(self.memory, self.ds, [room(0xee, '', 255, 0x3000)])
        with self.assertRaisesRegex(ValueError, 'duplicate object IDs'):
            inspect_exits(self.memory, self.ds, self.objects + [self.objects[0]])
        self.entry(2, 0x33, 2, 0x3000)
        with self.assertRaisesRegex(ValueError, 'conditional entry wraps segment'):
            inspect_exits(self.memory, self.ds, self.objects)
        self.entry(2, 0x33, 2, 0x27)
        struct.pack_into('<H', self.memory, self.ds * 16 + 0x8dd2, 0)
        with self.assertRaisesRegex(ValueError, 'no native conditional-exit-table segment'):
            inspect_exits(self.memory, self.ds, self.objects)
        struct.pack_into('<H', self.memory, self.ds * 16 + 0x8dd2,
                         self.conditional_segment)
        struct.pack_into('<H', self.memory, self.ds * 16 + 0x8dcc, 0)
        with self.assertRaisesRegex(ValueError, 'no native exit-table segment'):
            inspect_exits(self.memory, self.ds, self.objects)

    def test_cli_reads_actual_snapshot_layout_and_optional_object_names(self):
        # The original inspector walks two indirections to find the object
        # directory and descriptor; this exercises that integration too.
        ds = self.ds
        struct.pack_into('<H', self.memory, ds * 16 + 0x8d50, 0x2000)
        struct.pack_into('<H', self.memory, ds * 16 + 0x8d52, 0x3000)
        struct.pack_into('<H', self.memory, ds * 16 + 0x8d54, ds)
        struct.pack_into('<H', self.memory, ds * 16 + 0x38dc, 2)
        self.memory[0x30000 + 2] = 26  # type-1 descriptor: 26-byte record
        payload = b'Quarters\0Town\0'
        for object_id, state_offset, name_token, first, count in [
                (1, 0x100, 1, 1, 1), (2, 0x200, 10, 2, 2)]:
            descriptor = 0x20000 + 0xba + object_id * 6
            self.memory[descriptor] = 1
            struct.pack_into('<HH', self.memory, descriptor + 2,
                             state_offset, 0x4000)
            raw = room(object_id, '', count, first)['raw_state']
            self.memory[0x40000 + state_offset:0x40000 + state_offset + 26] = (
                bytes.fromhex(raw))
            struct.pack_into('<H', self.memory, 0x40000 + state_offset,
                             name_token)
            if object_id == 2:
                self.memory[0x40000 + state_offset + 4] = 0x01
        self.entry(0, 0x11, 1, 2)
        self.entry(1, 0x12, 1, 1)
        self.entry(2, 0x07, 2, 0x0e)
        struct.pack_into('<HH', self.memory,
                         self.conditional_segment * 16 + 0x4996 + 4 * 0x0e,
                         1, 2)
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory) / 'native'
            base.with_suffix('.bin').write_bytes(self.memory)
            base.with_suffix('.regs').write_text('DS=1000\n')
            names = Path(directory) / 'OBJECT.DAT'
            names.write_bytes(struct.pack('<H', len(payload)) + payload)
            command = [sys.executable, str(TOOL), str(base), '--names',
                       str(names), '--format', 'json']
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            data = json.loads(result.stdout)
            self.assertEqual([r['name'] for r in data['rooms']], ['Quarters', 'Town'])
            self.assertEqual(data['rooms'][0]['exits'][0]['direct_room'], 2)
            self.assertEqual(data['rooms'][1]['exits'][1]['conditional_room'], 1)
            self.assertEqual(data['rooms'][1]['exits'][1]['condition_object'], 2)
            with_flags = subprocess.run(command + ['--condition-flags'],
                                        capture_output=True, text=True)
            self.assertEqual(with_flags.returncode, 0, with_flags.stderr)
            flags = json.loads(with_flags.stdout)['rooms'][1]['exits'][1]['snapshot_flags']
            self.assertEqual(flags, {'0x0c': True, '0x10': False})
            graph = subprocess.run(command[:-1] + ['dot'], capture_output=True,
                                   text=True)
            self.assertEqual(graph.returncode, 0, graph.stderr)
            self.assertIn('n001 -> n002 [label="0x11"]', graph.stdout)
            optional = subprocess.run(command[:-1] + ['dot', '--include-conditional'],
                                      capture_output=True, text=True)
            self.assertEqual(optional.returncode, 0, optional.stderr)
            self.assertIn('n002 -> n001 [label="0x07", style=dashed', optional.stdout)
            invalid_format = subprocess.run(command + ['--include-conditional'],
                                            capture_output=True, text=True)
            self.assertEqual(invalid_format.returncode, 2)
            self.assertIn('requires --format dot', invalid_format.stderr)
            wrong = subprocess.run(command + ['--room', '0x3'],
                                   capture_output=True, text=True)
            self.assertEqual(wrong.returncode, 2)
            self.assertIn('unknown room ID', wrong.stderr)


if __name__ == '__main__':
    unittest.main()
