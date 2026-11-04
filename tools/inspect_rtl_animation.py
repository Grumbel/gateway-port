#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Resolve Your Quarters' four linked graphics calls to original RTL code.

Each OVL call operand is checked against the original source, then its guest
entry bytes are matched to GATE.EXE/GATE.RTL. No original executable data or
guest RAM is embedded in, or written by, this read-only inspector.
"""

import argparse
import json
from pathlib import Path
import re
import struct

from rank_engine_services import resolve_linked_origins


# Stable file offsets of the far CALL opcodes in GATEMAIN.OVL, section 38.
# Descriptions summarize separately inspected 8086 code in GATE.RTL.
ROOM_CALLS = (
    ('create', 0x51538, 0x1485, 0x1788,
     'allocate a sequence and return its slot handle'),
    ('initialize', 0x51543, 0x1485, 0x18da,
     'initialize active sequence frame indices and due times'),
    ('clear', 0x5155a, 0x1485, 0x1928,
     'clear existing sequence drawings and reset timing'),
    ('release_all', 0x5155f, 0x1485, 0x1bac,
     'release all active sequence buffers and zero their count'),
)

# File offsets of 8086 instructions whose DS-relative operands are fixed up
# by the original linker. Only opcode prefixes and *original* displacements
# are recorded here: the actual addresses must be read from live code.
STATE_OPERANDS = {
    'active_count': (0x13295, (0x83, 0x3e), 0x0012),
    'frame_count': (0x1335f, (0x88, 0x87), 0x0000),
    'direction': (0x13366, (0x88, 0x87), 0x0032),
    'frame_index': (0x1340b, (0x88, 0x84), 0x0040),
    'deadline': (0x13372, (0x89, 0x87), 0x0046),
    'frame_buffer': (0x132c4, (0x89, 0x87), 0x001e),
}


def inspect_animation_calls(ovl, exe, rtl, ram, load_segment=0x1000):
    """Return linked-to-file matches, rejecting changed or unresolved calls."""
    if not 0 <= load_segment <= 0xffff:
        raise ValueError('load segment must be a 16-bit unsigned value')
    targets = set()
    for name, source_offset, segment, offset, _note in ROOM_CALLS:
        if source_offset + 5 > len(ovl):
            raise ValueError(f'{name}: original OVL CALL site is out of bounds')
        opcode, target_offset, target_segment = struct.unpack_from(
            '<BHH', ovl, source_offset)
        if (opcode, target_segment, target_offset) != (0x9a, segment, offset):
            raise ValueError(f'{name}: original OVL linked far CALL changed')
        targets.add((segment, offset))
    mappings = resolve_linked_origins(targets, ram, exe, rtl, load_segment)
    results = []
    for name, source_offset, segment, offset, note in ROOM_CALLS:
        mapped = mappings.get((segment, offset))
        if mapped is None:
            raise ValueError(f'{name}: could not uniquely match the guest entry')
        if mapped['file'] != 'rtl':
            raise ValueError(f'{name}: matched {mapped["file"]}, not GATE.RTL')
        results.append(dict(name=name, caller_file='ovl',
                            caller_offset=source_offset,
                            linked_segment=segment, linked_offset=offset,
                            original_file=mapped['file'],
                            original_offset=mapped['offset'],
                            matched_bytes=mapped['matched_bytes'],
                            segment_support=mapped['segment_support'],
                            purpose=note))
    # The four services are parts of one resident RTL segment. This check
    # catches false matches in a different copy of the same byte signature.
    origins = {item['original_offset'] - item['linked_offset']
               for item in results}
    if len(origins) != 1:
        raise ValueError('far calls did not resolve to one consistent RTL origin')
    return results


def relocated_displacement(original, live, source_offset, guest_offset,
                           opcodes, original_displacement):
    """Read a linker-patched 16-bit displacement after matching its opcode."""
    expected = bytes(opcodes)
    if (original[source_offset:source_offset + 2] != expected or
            live[guest_offset:guest_offset + 2] != expected or
            source_offset + 4 > len(original) or guest_offset + 4 > len(live)):
        raise ValueError(f'RTL instruction mismatch at file 0x{source_offset:x}')
    if struct.unpack_from('<H', original, source_offset + 2)[0] != original_displacement:
        raise ValueError(f'original RTL data operand changed at 0x{source_offset:x}')
    return struct.unpack_from('<H', live, guest_offset + 2)[0]


def inspect_animation_state(rtl, ram, services, ds, load_segment=0x1000):
    """Decode sequence slot zero from relocated RTL code and one RAM dump.

    DS is captured from a *live RTL entry*; a checkpoint's SS alone cannot
    establish DS. The routine reads original code operands, never assuming
    the unrelocated DS offsets are the actual game-memory addresses.
    """
    if not 0 <= ds <= 0xffff:
        raise ValueError('DS must be a 16-bit segment')
    origins = {(load_segment + row['linked_segment']) * 16 +
               row['linked_offset'] - row['original_offset']
               for row in services}
    if len(origins) != 1:
        raise ValueError('animation services have inconsistent live code origins')
    origin = origins.pop()
    offsets = {
        name: relocated_displacement(rtl, ram, source, origin + source,
                                     opcodes, original_offset)
        for name, (source, opcodes, original_offset) in STATE_OPERANDS.items()
    }
    base = ds * 16

    def guest(offset, length):
        if offset + length > 0x10000 or base + offset + length > len(ram):
            raise ValueError(f'animation DS access 0x{offset:04x} out of range')
        return ram[base + offset:base + offset + length]

    count = struct.unpack('<H', guest(offsets['active_count'], 2))[0]
    if count > 5:
        raise ValueError(f'implausible active sequence count {count}')
    result = {'ds': ds, 'relocated_ds_offsets': offsets,
              'active_count': count}
    if count == 0:
        return result
    frame_count = guest(offsets['frame_count'], 1)[0]
    direction = struct.unpack('<b', guest(offsets['direction'], 1))[0]
    frame_index = struct.unpack('<b', guest(offsets['frame_index'], 1))[0]
    deadline = struct.unpack('<I', guest(offsets['deadline'], 4))[0]
    frame_offset, frame_segment = struct.unpack('<HH',
                                                guest(offsets['frame_buffer'], 4))
    if not (1 <= frame_count <= 20 and -1 <= direction <= 1 and
            -1 <= frame_index < frame_count):
        raise ValueError('implausible animation slot-zero state')
    result.update(frame_count=frame_count, direction=direction,
                  frame_index=frame_index, deadline=deadline,
                  frame_buffer=dict(segment=frame_segment, offset=frame_offset,
                                    linear=frame_segment * 16 + frame_offset))
    return result


WRITE_RE = re.compile(r'\bbyte-write ms=(\d+) address=([0-9a-f]+) '
                      r'pc=([0-9a-f]+).*\bvalue=([0-9a-f]+)\b')


def summarize_deadline_trace(lines, state, services, load_segment=0x1000):
    """Summarize 16-bit deadline updates at the RTL scheduler's store PC.

    The probe reports a separate pre-write callback for each watched byte.
    Combine the two little-endian bytes only when their guest millisecond
    timestamp and instruction PC match; ignore unrelated initializers.
    """
    if not state.get('active_count'):
        raise ValueError('cannot summarize deadlines without an active sequence')
    base = (load_segment + services[0]['linked_segment']) * 16 + \
           services[0]['linked_offset'] - services[0]['original_offset']
    store_pc = base + 0x13554  # mov DX,DS:[deadline+4*index] (write lower word)
    low = state['ds'] * 16 + state['relocated_ds_offsets']['deadline']
    pending = {}
    points = []
    for line in lines:
        match = WRITE_RE.search(line)
        if match is None:
            continue
        ms, address, pc, value = (int(match[1]), int(match[2], 16),
                                  int(match[3], 16), int(match[4], 16))
        if pc != store_pc or address not in (low, low + 1):
            continue
        slot = pending.setdefault(ms, {})
        slot[address - low] = value & 0xff
        if len(slot) == 2:
            points.append((ms, slot[0] | (slot[1] << 8)))
            del pending[ms]
    points.sort()
    from collections import Counter
    deadlines = Counter((end[1] - begin[1]) & 0xffff
                        for begin, end in zip(points, points[1:]))
    guest_times = Counter(end[0] - begin[0]
                          for begin, end in zip(points, points[1:]))
    return {'updates': len(points), 'store_pc': store_pc,
            'deadline_address': low, 'first': points[0] if points else None,
            'last': points[-1] if points else None,
            'deadline_step_counts': dict(sorted(deadlines.items())),
            'guest_ms_step_counts': dict(sorted(guest_times.items()))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ovl', type=Path, required=True,
                        help='original GATEMAIN.OVL')
    parser.add_argument('--exe', type=Path, required=True,
                        help='original GATE.EXE')
    parser.add_argument('--rtl', type=Path, required=True,
                        help='original GATE.RTL')
    parser.add_argument('--ram', type=Path, required=True,
                        help='guest RAM dump containing the loaded resident RTL code')
    parser.add_argument('--load-segment', type=lambda value: int(value, 0),
                        default=0x1000)
    parser.add_argument('--ds', type=lambda value: int(value, 16),
                        help='DS segment observed at a live RTL entry (hex); also decode slot zero')
    parser.add_argument('--write-trace', type=Path,
                        help='probe research trace with deadline byte-write watches (requires --ds)')
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    if args.write_trace is not None and args.ds is None:
        parser.error('--write-trace requires --ds')
    try:
        rtl, ram = args.rtl.read_bytes(), args.ram.read_bytes()
        rows = inspect_animation_calls(args.ovl.read_bytes(), args.exe.read_bytes(),
                                       rtl, ram, args.load_segment)
        state = (inspect_animation_state(rtl, ram, rows, args.ds, args.load_segment)
                 if args.ds is not None else None)
        cadence = None
        if args.write_trace is not None:
            with args.write_trace.open() as stream:
                cadence = summarize_deadline_trace(stream, state, rows,
                                                   args.load_segment)
    except ValueError as exc:
        parser.error(str(exc))
    if args.json:
        output = {'services': rows}
        if state is not None:
            output['state'] = state
        if cadence is not None:
            output['deadline_trace'] = cadence
        print(json.dumps(output, indent=2))
    else:
        for row in rows:
            print(f'{row["name"]:<12} ovl:0x{row["caller_offset"]:06x} '
                  f'{row["linked_segment"]:04x}:{row["linked_offset"]:04x} '
                  f'=> {row["original_file"]}:0x{row["original_offset"]:05x} '
                  f'({row["matched_bytes"]}-byte match, '
                  f'{row["segment_support"]} supporting entries)')
            print(f'  {row["purpose"]}')
        print('These are original-source identities, not a verified host port.')
        if state is not None:
            offsets = state['relocated_ds_offsets']
            print(f'RTL entry DS={state["ds"]:04x}; active sequences='
                  f'{state["active_count"]} at DS:{offsets["active_count"]:04x}')
            if state['active_count']:
                print(f'slot 0: {state["frame_count"]} frame(s), '
                      f'direction={state["direction"]}, '
                      f'current={state["frame_index"]}, '
                      f'deadline=0x{state["deadline"]:08x}, '
                      f'frame buffer={state["frame_buffer"]["segment"]:04x}:'
                      f'{state["frame_buffer"]["offset"]:04x}')
            print('Relocated DS fields: ' + ', '.join(
                f'{name}={offset:04x}' for name, offset in offsets.items()))
        if cadence is not None:
            print(f'Scheduler deadline writes: {cadence["updates"]} from '
                  f'PC={cadence["store_pc"]:05x} to DS:{offsets["deadline"]:04x}')
            print('Deadline increments: ' + ', '.join(
                f'{step} x {hits}' for step, hits in
                cadence['deadline_step_counts'].items()))
            print('Guest-ms increments: ' + ', '.join(
                f'{step} x {hits}' for step, hits in
                cadence['guest_ms_step_counts'].items()))


if __name__ == '__main__':
    main()
