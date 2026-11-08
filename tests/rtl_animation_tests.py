#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Synthetic and optional original-image tests for RTL animation services."""

import hashlib
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from inspect_rtl_animation import (
    DRAW_CALL_SITES, ROOM_CALLS, VFX_STATE_FAR_CALLS, VFX_STATE_OPERANDS,
    inspect_animation_calls, inspect_vfx_state,
    inspect_animation_draw_calls, inspect_animation_state,
    mode3_rasterizer_addresses, relocated_displacement,
    resolve_animation_draw_origins, resolve_vfx_far_sources,
    summarize_deadline_trace,
    summarize_mode3_trace)
from prepare_intro_ghidra import RTL_SHA256


class AnimationServiceTests(unittest.TestCase):
    def test_vfx_callback_origin_matching_refuses_uninitialized_memory(self):
        rtl = bytearray(0x19000)
        ram = bytearray(1 << 20)
        source = 0x184e8
        segment, offset = 0x2a1c, 0x000c
        code = bytes((i * 13 + 17) & 255 for i in range(16))
        rtl[source:source + 16] = code
        ram[segment * 16 + offset:segment * 16 + offset + 16] = code
        calls = {'draw': {'segment': segment, 'offset': offset},
                 'unknown': {'segment': 0x2500, 'offset': 0x003e},
                 'duplicate': {'segment': segment, 'offset': offset}}
        resolved = resolve_vfx_far_sources(b'MZ' + bytes(100), rtl, ram, calls)
        self.assertEqual(resolved['draw']['file'], 'rtl')
        self.assertEqual(resolved['draw']['offset'], source)
        self.assertEqual(resolved['draw']['matched_bytes'], 16)
        self.assertEqual(resolved['draw'], resolved['duplicate'])
        self.assertIsNone(resolved['unknown'])

    def test_missing_snapshot_has_reproducible_capture_instructions(self):
        with tempfile.TemporaryDirectory() as directory:
            snapshot = Path(directory) / 'missing.bin'
            command = [sys.executable, str(ROOT / 'tools/inspect_rtl_animation.py'),
                       '--ovl', 'unused.ovl', '--exe', 'unused.exe',
                       '--rtl', 'unused.rtl', '--ram', str(snapshot), '--vfx-state']
            missing = subprocess.run(command, text=True, capture_output=True,
                                     check=False)
            self.assertEqual(missing.returncode, 2)
            self.assertIn('guest RAM snapshot not found:', missing.stderr)
            self.assertIn('--capture-labels', missing.stderr)
            self.assertIn('checkpoint-book_taken.bin', missing.stderr)
            self.assertNotIn('Traceback', missing.stderr)
            snapshot.write_bytes(b'not a RAM image')
            invalid = subprocess.run(command, text=True, capture_output=True,
                                     check=False)
            self.assertEqual(invalid.returncode, 2)
            self.assertIn('must be exactly 1048576 bytes', invalid.stderr)

    def test_vfx_save_restore_relocation_evidence_and_branch_selection(self):
        """Synthetic RTL and RAM: never embed the proprietary routine bytes."""
        rtl = bytearray(0x19000)
        ram = bytearray(1 << 20)
        origin = 0x13031
        entries = [('save_video_state', 0x11078, 0x04d9),
                   ('restore_video_state', 0x10f64, 0x03c5)]
        drawing = [{'name': name, 'callee_source': {'offset': source},
                    'runtime_target': {'linear': 0x23bd0 + entry}}
                   for name, source, entry in entries]
        for name, (source, opcodes, original) in VFX_STATE_OPERANDS.items():
            size = len(opcodes) + 2
            rtl[source:source + size] = bytes(opcodes) + struct.pack('<H', original)
            relocated = 0x2000 + original
            ram[origin + source:origin + source + size] = bytes(opcodes) + \
                struct.pack('<H', relocated)
        for index, (name, source) in enumerate(VFX_STATE_FAR_CALLS.items()):
            rtl[source:source + 5] = b'\x9a\x00\x00\x00\x00'
            struct.pack_into('<BHH', ram, origin + source, 0x9a,
                             0x0100 + index, 0x4100)
        ds = 0x2a7a
        struct.pack_into('<H', ram, ds * 16 + 0x2000, 1)
        report = inspect_vfx_state(rtl, ram, drawing, ds)
        self.assertEqual(report['original_entries'],
                         {'save_video_state': 0x11078,
                          'restore_video_state': 0x10f64})
        self.assertEqual(set(report['relocated_ds_offsets'].values()),
                         {0x2000, 0x2002, 0x2004, 0x2098, 0x209a,
                          0x2100, 0x2102})
        self.assertEqual(report['resident_far_calls']['save_request_33'],
                         {'segment': 0x4100, 'offset': 0x0101})
        self.assertTrue(report['save_fast_path'])
        self.assertFalse(report['restore_fast_path_if_saved_nonzero'])
        struct.pack_into('<H', ram, ds * 16 + 0x2000, 2)
        report = inspect_vfx_state(rtl, ram, drawing, ds)
        self.assertFalse(report['save_fast_path'])
        self.assertTrue(report['restore_fast_path_if_saved_nonzero'])
        ram[origin + VFX_STATE_OPERANDS['restore_flags_test'][0] + 2] ^= 1
        with self.assertRaisesRegex(ValueError, 'inconsistent VFX flags'):
            inspect_vfx_state(rtl, ram, drawing)
        ram[origin + VFX_STATE_OPERANDS['restore_flags_test'][0] + 2] ^= 1
        struct.pack_into('<HH', ram,
                         origin + VFX_STATE_FAR_CALLS['restore_request_33'] + 1,
                         0, 0)
        with self.assertRaisesRegex(ValueError, 'was not relocated'):
            inspect_vfx_state(rtl, ram, drawing)

    def test_vfx_state_rejects_wrong_rtl_identity_and_missing_call(self):
        with self.assertRaisesRegex(ValueError, 'missing verified original'):
            inspect_vfx_state(b'', b'', [{'name': 'save_video_state',
                                          'callee_source': {'offset': 0x11078}}])

    def test_optional_original_vfx_save_restore_instructions(self):
        original = ROOT.parent / 'gateway-dos/GATE.RTL'
        if not original.is_file():
            original = ROOT.parent / 'original-inputs/gateway-dos/GATE.RTL'
        if not original.is_file():
            self.skipTest('original GATE.RTL unavailable')
        rtl = original.read_bytes()
        if hashlib.sha256(rtl).hexdigest() != RTL_SHA256:
            self.skipTest('GATE.RTL differs from pinned original')
        self.assertEqual(rtl[0x10f64:0x10f67], b'\x55\x8b\xec')
        self.assertEqual(rtl[0x11077], 0xcb)
        self.assertEqual(rtl[0x11078:0x1107b], b'\x55\x8b\xec')
        self.assertEqual(rtl[0x1111f], 0xcb)
        for name, (offset, opcodes, displacement) in VFX_STATE_OPERANDS.items():
            with self.subTest(operand=name):
                self.assertEqual(rtl[offset:offset + len(opcodes)], bytes(opcodes))
                self.assertEqual(struct.unpack_from('<H', rtl,
                                 offset + len(opcodes))[0], displacement)
        for name, offset in VFX_STATE_FAR_CALLS.items():
            with self.subTest(call=name):
                self.assertEqual(rtl[offset:offset + 5], b'\x9a\0\0\0\0')

    def setUp(self):
        self.ovl = bytearray(0x51570)
        self.exe = b'MZ' + bytes(0x800)
        self.rtl = bytearray(0x4000)
        self.ram = bytearray(1 << 20)
        self.origin = 0x400
        for index, (_, call, segment, offset, _) in enumerate(ROOM_CALLS):
            struct.pack_into('<BHH', self.ovl, call, 0x9a, offset, segment)
            # Unique, fully synthetic 32-byte entries at a common source base.
            code = bytes(((value * 19 + index * 31 + 7) % 256)
                         for value in range(32))
            source_offset = self.origin + offset
            guest_linear = (0x1000 + segment) * 16 + offset
            self.rtl[source_offset:source_offset + len(code)] = code
            self.ram[guest_linear:guest_linear + len(code)] = code

    def resolve(self):
        return inspect_animation_calls(self.ovl, self.exe, self.rtl, self.ram)

    def test_four_synthetic_far_calls_have_common_rtl_origin(self):
        rows = self.resolve()
        self.assertEqual(len(rows), 4)
        self.assertEqual([row['original_offset'] for row in rows],
                         [self.origin + item[3] for item in ROOM_CALLS])
        self.assertTrue(all(row['matched_bytes'] == 16 for row in rows))
        self.assertTrue(all(row['segment_support'] == 4 for row in rows))

    def test_changed_overlay_call_is_rejected(self):
        self.ovl[ROOM_CALLS[0][1]] = 0x90
        with self.assertRaisesRegex(ValueError, 'linked far CALL changed'):
            self.resolve()

    def test_unresolved_guest_entry_is_rejected(self):
        _, _, segment, offset, _ = ROOM_CALLS[2]
        guest_linear = (0x1000 + segment) * 16 + offset
        self.ram[guest_linear:guest_linear + 128] = b'\x00' * 128
        with self.assertRaisesRegex(ValueError, 'could not uniquely match'):
            self.resolve()

    def test_decode_relocated_operand_without_assuming_a_data_base(self):
        original = bytes((0x83, 0x3e, 0x12, 0x00, 0x05))
        live = bytes((0x83, 0x3e, 0x28, 0x14, 0x05))
        self.assertEqual(relocated_displacement(original, live, 0, 0,
                                                (0x83, 0x3e), 0x12), 0x1428)
        with self.assertRaisesRegex(ValueError, 'instruction mismatch'):
            relocated_displacement(original, b'\x90' + live[1:], 0, 0,
                                   (0x83, 0x3e), 0x12)
        with self.assertRaisesRegex(ValueError, 'original RTL data operand changed'):
            relocated_displacement(original, live, 0, 0,
                                   (0x83, 0x3e), 0x14)

    def test_deadline_trace_reconstructs_two_byte_stores(self):
        rows = self.resolve()
        state = {'ds': 0x5b46, 'active_count': 1,
                 'relocated_ds_offsets': {'deadline': 0x9ffa}}
        base = (0x1000 + rows[0]['linked_segment']) * 16 + \
               rows[0]['linked_offset'] - rows[0]['original_offset']
        pc = base + 0x13554
        watched = state['ds'] * 16 + 0x9ffa
        lines = []
        for ms, deadline in ((1000, 0x152c), (1501, 0x1720), (2000, 0x1914)):
            for index in (0, 1):
                lines.append(f'byte-write ms={ms} address={watched + index:x} '
                             f'pc={pc:x} size=2 before=0 '
                             f'value={(deadline >> (8 * index)) & 0xff:x}\n')
        lines.append(f'byte-write ms=2500 address={watched:x} '
                     f'pc=12345 size=2 before=0 value=5\n')
        report = summarize_deadline_trace(lines, state, rows)
        self.assertEqual(report['updates'], 3)
        self.assertEqual(report['deadline_step_counts'], {500: 2})
        self.assertEqual(report['guest_ms_step_counts'], {499: 1, 501: 1})

    def test_frame_renderer_calls_are_resolved_from_live_relocations(self):
        rtl = bytearray(0x14000)
        ram = bytearray(1 << 20)
        origin = 0x25fd8 - 0x1328d
        signature = bytes.fromhex('0ee867f9')
        rtl[0x1356c:0x13570] = signature
        ram[origin + 0x1356c:origin + 0x13570] = signature
        expected = ((0x23bd, 0x04d9), (0x2a1c, 0x000c),
                    (0x23bd, 0x03c5))
        for (_, source), (segment, offset) in zip(DRAW_CALL_SITES, expected):
            struct.pack_into('<BHH', rtl, source, 0x9a, 0, 0)
            struct.pack_into('<BHH', ram, origin + source, 0x9a,
                             offset, segment)
        rows = [{'linked_segment': 0x1485, 'linked_offset': 0x1788,
                 'original_offset': 0x1328d}]
        calls = inspect_animation_draw_calls(rtl, ram, rows)
        self.assertEqual([item['name'] for item in calls],
                         [item[0] for item in DRAW_CALL_SITES])
        self.assertEqual([(item['runtime_target']['segment'],
                           item['runtime_target']['offset']) for item in calls],
                         list(expected))
        ram[origin + DRAW_CALL_SITES[1][1]] = 0x90
        with self.assertRaisesRegex(ValueError, 'frame draw CALL mismatch'):
            inspect_animation_draw_calls(rtl, ram, rows)

    def test_resolve_frame_draw_origins_and_mode3_backend_trace(self):
        rtl = bytearray(0x19000)
        ram = bytearray(1 << 20)
        calls = []
        for index, (name, call_site) in enumerate(DRAW_CALL_SITES):
            segment, offset, original = (
                (0x23bd, 0x04d9, 0x11078),
                (0x2a1c, 0x000c, 0x184e8),
                (0x23bd, 0x03c5, 0x10f64))[index]
            signature = bytes((i * 17 + index * 51 + 3) % 256
                              for i in range(16))
            rtl[original:original + len(signature)] = signature
            address = segment * 16 + offset
            ram[address:address + len(signature)] = signature
            calls.append({'name': name, 'original_file': 'rtl',
                          'original_offset': call_site,
                          'runtime_target': dict(segment=segment, offset=offset,
                                                 linear=address)})
        resolved = resolve_animation_draw_origins(b'MZ', rtl, ram, calls)
        self.assertEqual([row['callee_source']['offset'] for row in resolved],
                         [0x11078, 0x184e8, 0x10f64])
        self.assertTrue(all(row['callee_source']['matched_bytes'] == 16
                            for row in resolved))

        rtl[0x18722:0x18725] = bytes.fromhex('3d0300')
        rtl[0x18736:0x18739] = bytes.fromhex('263105')
        rtl[0x18745:0x18748] = bytes.fromhex('263105')
        rtl[0x1885b:0x18869] = bytes.fromhex('b103d3e3b003bace03ee428ac3ee')
        branches = mode3_rasterizer_addresses(rtl, resolved)
        self.assertEqual(branches, {'packed': 0x2a406, 'vga': 0x2a4be})
        trace = summarize_mode3_trace((
            'entry ms=100 pc=2a1cc cs=2a1c\n',
            'entry ms=200 pc=2a4be cs=2a1c\n',
            'entry ms=700 pc=2a4be cs=2a1c\n',
            'entry ms=900 pc=2a406 cs=2a1c\n'), branches)
        self.assertEqual(trace['vga'], {'hits': 2, 'first_ms': 200,
                                        'last_ms': 700})
        self.assertEqual(trace['packed']['hits'], 1)
        rtl[0x18736] = 0x90
        with self.assertRaisesRegex(ValueError, 'mode-3 XOR paths changed'):
            mode3_rasterizer_addresses(rtl, resolved)

    def test_optional_original_rtl_animation_services(self):
        dos = ROOT.parent / 'gateway-dos'
        snapshot = ROOT / 'build/rgn-scenes-v026/before.bin'
        files = [dos / 'GATEMAIN.OVL', dos / 'GATE.EXE',
                 dos / 'GATE.RTL', snapshot]
        if not all(file.exists() for file in files):
            self.skipTest('original DOS files and captured guest RAM unavailable')
        ovl, exe, rtl, ram = (file.read_bytes() for file in files)
        rows = inspect_animation_calls(ovl, exe, rtl, ram)
        self.assertEqual([row['original_offset'] for row in rows],
                         [0x1328d, 0x133df, 0x1342d, 0x136b1])
        draw_calls = inspect_animation_draw_calls(rtl, ram, rows)
        self.assertEqual([(row['runtime_target']['segment'],
                           row['runtime_target']['offset']) for row in draw_calls],
                         [(0x23bd, 0x04d9), (0x2a1c, 0x000c),
                          (0x23bd, 0x03c5)])
        resolved = resolve_animation_draw_origins(exe, rtl, ram, draw_calls)
        self.assertEqual([row['callee_source']['offset'] for row in resolved],
                         [0x11078, 0x184e8, 0x10f64])
        self.assertTrue(all(row['callee_source']['matched_bytes'] == 16
                            for row in resolved))
        branches = mode3_rasterizer_addresses(rtl, resolved)
        self.assertEqual(branches, {'packed': 0x2a406, 'vga': 0x2a4be})
        trace = ROOT / 'build/mode3-v035/native.trace'
        if trace.exists():
            with trace.open() as stream:
                observed = summarize_mode3_trace(stream, branches)
            self.assertEqual(observed['packed']['hits'], 0)
            self.assertGreaterEqual(observed['vga']['hits'], 15)

        # Read-only checks of original machine-code invariants: five active
        # sequence slots, 20 frame records, 0x88-byte frame stride, and a
        # global active-count reset. The RTL service's DS register is not
        # established by the game's DS:5b46 checkpoint alone.
        self.assertEqual(rtl[0x13295:0x132a2], bytes.fromhex(
            '833e1200057406837e08147e09'))
        self.assertIn(bytes.fromhex('81c68800'), rtl[0x1328d:0x13387])
        self.assertEqual(rtl[0x1373a:0x13740],
                         bytes.fromhex('c70612000000'))
        self.assertIn(bytes.fromhex('b03226f66707'),
                      rtl[0x134a5:0x136b1])

        # Live entry traces establish DS=5b46 for these services. The raw
        # file's small displacements are relocated to two distinct regions
        # of that segment, and all four guest snapshots contain valid slots.
        expected_deadlines = {'before': 0x30c0, 'open': 0x3fe8,
                              'taken': 0x56fe, 'closed': 0x78aa}
        expected_offsets = {'active_count': 0x1428, 'frame_count': 0x9fb4,
                            'direction': 0x9fe6, 'frame_index': 0x9ff4,
                            'deadline': 0x9ffa, 'frame_buffer': 0x9fd2}
        for name, deadline in expected_deadlines.items():
            memory = (snapshot.parent / (name + '.bin')).read_bytes()
            state = inspect_animation_state(rtl, memory, rows, 0x5b46)
            self.assertEqual(state['relocated_ds_offsets'], expected_offsets)
            self.assertEqual(state['active_count'], 1)
            self.assertEqual(state['frame_count'], 1)
            self.assertEqual(state['direction'], 0)
            self.assertEqual(state['frame_index'], 0)
            self.assertEqual(state['deadline'], deadline)
            self.assertEqual(state['frame_buffer'], dict(segment=0x81d2,
                                                          offset=0, linear=0x81d20))

        trace = ROOT / 'build/rtl-timing-v033/native.trace'
        if trace.exists():
            with trace.open() as stream:
                report = summarize_deadline_trace(stream, state, rows)
            self.assertGreaterEqual(report['updates'], 50)
            self.assertGreaterEqual(report['deadline_step_counts'].get(500, 0), 50)

    def test_optional_original_idle_indicator_blink(self):
        directory = ROOT / 'build/rtl-blink-v033'
        times = (16000, 16250, 16500, 16750,
                 17000, 17250, 17500, 17750)
        captures = [directory / f't{ms}.ppm' for ms in times]
        if not all(path.exists() for path in captures):
            self.skipTest('original idle animation framebuffer series unavailable')
        header = b'P6\n640 480\n255\n'
        frames = []
        for path in captures:
            data = path.read_bytes()
            self.assertTrue(data.startswith(header))
            self.assertEqual(len(data), len(header) + 640 * 480 * 3)
            frames.append(data[len(header):])

        # The only changing 50 pixels are the same physical pixels at
        # every transition; identical 250 ms pairs bracket each 500 ms
        # XOR toggle. All other 307,150 framebuffer pixels remain equal.
        changes = []
        for before, after in zip(frames, frames[1:]):
            changes.append({index for index in range(640 * 480)
                            if before[index * 3:index * 3 + 3] !=
                            after[index * 3:index * 3 + 3]})
        self.assertEqual([len(change) for change in changes],
                         [0, 50, 0, 50, 0, 50, 0])
        self.assertEqual(changes[1], changes[3])
        self.assertEqual(changes[1], changes[5])
        self.assertEqual(frames[0], frames[4])
        self.assertEqual(frames[2], frames[6])

    def test_optional_original_drawer_and_book_animation_timeline(self):
        """Separate the 500-ms sequence from drawer and object image updates."""
        directory = ROOT / 'build/rtl-actions-v034'
        scene_tool = ROOT / 'build/headless-local/gateway-scene'
        pic = ROOT.parent / 'gateway-dos/GATE_102.PIC'
        timestamps = (19500, 19750, 20000, 20250, 20500, 20750,
                      21000, 21500, 21750, 22000, 22250, 22500,
                      23000, 25000, 25500, 25750, 26000, 26250,
                      26500, 26750, 27000, 30500, 31000, 31500,
                      32000, 34500, 34750, 35000, 35250, 35500,
                      35750, 36000)
        if (not pic.exists() or not scene_tool.is_file() or
                not all((directory / f't{ms}.ppm').is_file() and
                        (directory / f't{ms}.bin').is_file()
                        for ms in timestamps)):
            self.skipTest('original drawer/book framebuffer series unavailable')
        # Independent PIC inference from the native framebuffer: layer 16
        # remains present while the drawer is open; layer 17 continues to
        # blink before and after close; layer 18 remains present.
        indicator_on = {19750, 20000, 20750, 21000, 21500, 22250,
                        22500, 25500, 26250, 26500, 30500, 31500,
                        34750, 35000, 35750, 36000}
        for ms in timestamps:
            frame = directory / f't{ms}.ppm'
            result = subprocess.run([str(scene_tool), '--base', '15',
                                     '--optional-layer', '16',
                                     '--optional-layer', '17',
                                     '--optional-layer', '18',
                                     '--reference-ppm', str(frame),
                                     '--screen-x', '261', str(pic)],
                                    text=True, capture_output=True, check=True)
            match = re.search(r': (\d+)/(\d+) differing scene pixels; '
                              r'(\d+) equally good layer sets; selected entries:(.*)',
                              result.stdout)
            self.assertIsNotNone(match, f'{ms}: unexpected scene report')
            self.assertEqual((int(match[1]), int(match[2]), int(match[3])),
                             (0, 84896, 1), f'{ms}: image diverged')
            layers = set(map(int, match[4].split()))
            expected = {18}
            if ms < 34500:
                expected.add(16)
            if ms in indicator_on:
                expected.add(17)
            self.assertEqual(layers, expected, f'{ms}: wrong layer set')

            ram = (directory / f't{ms}.bin').read_bytes()
            base = 0x5b46 * 16  # established from live RTL entry trace
            self.assertEqual(struct.unpack_from('<H', ram, base + 0x1428)[0], 1)
            self.assertEqual(struct.unpack_from('<h', ram, base + 0x6a20)[0], 0)
            self.assertEqual(ram[base + 0x69fe], 1)
            self.assertEqual(ram[base + 0xadf8], int(ms < 34500))


if __name__ == '__main__':
    unittest.main()
