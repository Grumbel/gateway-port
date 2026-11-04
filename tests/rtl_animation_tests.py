#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Synthetic and optional original-image tests for RTL animation services."""

from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from inspect_rtl_animation import (
    ROOM_CALLS, inspect_animation_calls, inspect_animation_state,
    relocated_displacement, summarize_deadline_trace)


class AnimationServiceTests(unittest.TestCase):
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


if __name__ == '__main__':
    unittest.main()
