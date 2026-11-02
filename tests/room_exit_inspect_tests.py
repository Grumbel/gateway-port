"""Gateway's four-byte native room exit table, without proprietary assets."""
from pathlib import Path
import json
import struct
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from inspect_room_exits import format_dot, format_text, inspect_exits


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
        struct.pack_into('<H', self.memory, self.ds * 16 + 0x8dcc,
                         self.table_segment)
        self.objects = [room(0xee, 'Your Quarters', 1, 1),
                        room(0xf1, 'Heecheetown', 2, 2),
                        room(0xf2, 'Inactive', 2, 0),
                        dict(object=0xd3, type=7, name='you')]
        self.entry(0, 0x11, 1, 0xf1)
        self.entry(1, 0x12, 1, 0xee)
        self.entry(2, 0x33, 2, 0x27)

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
        self.assertFalse(inactive['active'])
        self.assertEqual(inactive['exits'], [])
        report = format_text(snapshot)
        self.assertIn('0x0ee Your Quarters: count=1 first=1', report)
        self.assertIn('direction=0x11 kind=1 (direct-room) argument=0x0f1', report)
        self.assertIn('native dispatcher skips', report)
        self.assertIn('not verified reachable', report)

    def test_dot_uses_only_direct_edges_and_escapes_names(self):
        self.objects[1]['name'] = 'Heecheetown "A"'
        graph = format_dot(inspect_exits(self.memory, self.ds, self.objects))
        self.assertIn('digraph gateway_native_exits', graph)
        self.assertIn('n0ee -> n0f1 [label="0x11"]', graph)
        self.assertIn('n0f1 -> n0ee [label="0x12"]', graph)
        self.assertIn('Heecheetown \\"A\\"', graph)
        self.assertNotIn('0x33', graph)  # conditional branch is not a direct room edge

    def test_preserves_unknown_kinds_and_unresolved_direct_targets(self):
        self.entry(0, 0xaa, 9, 0xf1)
        self.entry(1, 0xbb, 1, 0x999)
        world = inspect_exits(self.memory, self.ds, self.objects)
        self.assertEqual(world['rooms'][0]['exits'][0]['kind_meaning'], 'unknown')
        self.assertNotIn('direct_room', world['rooms'][1]['exits'][0])
        self.assertNotIn('n0ee', format_dot(world))

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
        with self.assertRaisesRegex(ValueError, 'exit table wraps segment'):
            inspect_exits(self.memory, self.ds, [room(0xee, '', 255, 0x3000)])
        with self.assertRaisesRegex(ValueError, 'duplicate object IDs'):
            inspect_exits(self.memory, self.ds, self.objects + [self.objects[0]])
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
        for object_id, state_offset, name_token, first in [
                (1, 0x100, 1, 1), (2, 0x200, 10, 2)]:
            descriptor = 0x20000 + 0xba + object_id * 6
            self.memory[descriptor] = 1
            struct.pack_into('<HH', self.memory, descriptor + 2,
                             state_offset, 0x4000)
            raw = room(object_id, '', 1, first)['raw_state']
            self.memory[0x40000 + state_offset:0x40000 + state_offset + 26] = (
                bytes.fromhex(raw))
            struct.pack_into('<H', self.memory, 0x40000 + state_offset,
                             name_token)
        self.entry(0, 0x11, 1, 2)
        self.entry(1, 0x12, 1, 1)
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
            graph = subprocess.run(command[:-1] + ['dot'], capture_output=True,
                                   text=True)
            self.assertEqual(graph.returncode, 0, graph.stderr)
            self.assertIn('n001 -> n002 [label="0x11"]', graph.stdout)
            wrong = subprocess.run(command + ['--room', '0x3'],
                                   capture_output=True, text=True)
            self.assertEqual(wrong.returncode, 2)
            self.assertIn('unknown room ID', wrong.stderr)


if __name__ == '__main__':
    unittest.main()
