#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Read Gateway's native 195-entry action table from a live memory dump.

Gateway's resident dispatch code reads a selector byte at 0x27 + 6*action
and a far handler pointer at 0x28 + 6*action. The following byte is separate:
reading a 16-bit "other" word there mixes it with the next action's selector.
A nonresident action's pointer leads to the START of a ten-byte RTLink
trampoline (near CALL to the manager gate, followed by a far JMP and section
ID), not the embedded reference at offset +4.
"""
import argparse
import hashlib
import json
import pathlib
import struct
from prepare_ghidra import EXPECTED_HASHES
from rtlink_trampoline import decode_trampoline, map_reference, sections_by_id


ACTION_COUNT = 195
KNOWN_ACTIONS = {0x1d: 'close drawer', 0x32: 'drop book', 0x46: 'take book',
                 0x53: 'inventory', 0x5f: 'look', 0x71: 'open drawer'}


def inspect_actions(memory, ds, action_ids):
    def byte(segment, offset):
        address = segment * 16 + offset
        if address < 0 or address >= min(len(memory), 0xa0000):
            raise ValueError('action table byte outside guest conventional memory')
        return memory[address]

    def word(segment, offset):
        address = segment * 16 + offset
        if address < 0 or address + 2 > min(len(memory), 0xa0000):
            raise ValueError('action table word outside guest conventional memory')
        return struct.unpack_from('<H', memory, address)[0]

    if not 0 < ds <= 0xffff:
        raise ValueError('invalid original Gateway data segment')
    directory_segment = word(ds, 0x8da8)
    if not directory_segment:
        raise ValueError('no live action directory at DS:8da8')
    results = []
    for action in action_ids:
        if not 1 <= action <= ACTION_COUNT:
            raise ValueError(f'action {action} outside 1..{ACTION_COUNT}')
        offset = 0x28 + 6 * action
        ip = word(directory_segment, offset)
        cs = word(directory_segment, offset + 2)
        selector = byte(directory_segment, offset - 1)
        trailer = byte(directory_segment, offset + 4)
        entry = dict(action=action, handler_segment=cs, handler_offset=ip,
                     selector=selector, trailer=trailer)
        if selector:
            # GATEMAIN.EXE file 0x5981–0x5987: BX=selector*4 then indirect
            # CALL ES:[BX-4]. The table lives in the same segment as actions.
            # Preserve this raw live far pointer; it is not yet a proven
            # permission check or an RTLink reference.
            callback_offset = 4 * (selector - 1)
            entry['selector_callback'] = dict(segment=word(directory_segment, callback_offset + 2),
                                              offset=word(directory_segment, callback_offset))
        if reference := decode_trampoline(memory, cs, ip):
            entry['rtlink_reference'] = reference
        if selector and (reference := decode_trampoline(
                memory, entry['selector_callback']['segment'],
                entry['selector_callback']['offset'])):
            entry['selector_reference'] = reference
        results.append(entry)
    return results


def map_action_entries(entries, manifest):
    sections = sections_by_id(manifest)
    output = []
    for item in entries:
        result = dict(item)
        for reference_key, native_key in (('rtlink_reference', 'native_code'),
                                          ('selector_reference', 'selector_native_code')):
            ref = item.get(reference_key)
            if not ref:
                continue
            result[native_key] = map_reference(ref, sections)
        output.append(result)
    return output


def map_resident_actions(entries, memory, exe, relocation,
                         expected_sha256=EXPECTED_HASHES['GATEMAIN.EXE']):
    """Map four resident pointers with unique code signatures and relocation checks."""
    if hashlib.sha256(exe).hexdigest() != expected_sha256 or exe[:2] != b'MZ':
        raise ValueError('resident action mapping requires the fingerprinted main EXE')
    if (len(entries) != ACTION_COUNT or
            {item['action'] for item in entries} != set(range(1, ACTION_COUNT + 1))):
        raise ValueError('resident mapping requires all 195 distinct actions')
    residents = [item for item in entries if 'rtlink_reference' not in item]
    if {item['action'] for item in residents} != {0x5f, 0x6d, 0x98, 0xaf}:
        raise ValueError('unexpected resident action IDs in native table')
    if len({item['handler_segment'] for item in residents}) != 1:
        raise ValueError('resident actions do not share a code segment')
    if not 0 <= relocation <= 0xffff:
        raise ValueError('invalid RTLink relocation factor')

    deltas = set()
    result = []
    for item in entries:
        mapped = dict(item)
        if item in residents:
            address = item['handler_segment'] * 16 + item['handler_offset']
            if not 0 <= address <= min(len(memory), 0xa0000) - 96:
                raise ValueError('resident action pointer outside guest RAM')
            prefix = memory[address:address + 8]
            offset = exe.find(prefix)
            if offset < 0 or exe.find(prefix, offset + 1) >= 0 or offset + 96 > len(exe):
                raise ValueError('resident action signature is not unique in original EXE')
            deltas.add(offset - item['handler_offset'])

            # RTLink adjusts the segment of far CALL (9a) operands at load.
            # A 96-byte neighborhood must otherwise match byte for byte.
            for index in range(96):
                if exe[offset + index] == memory[address + index]:
                    continue
                if index < 4 or exe[offset + index - 4] != 0x9a:
                    raise ValueError('resident action bytes differ outside far-call relocation')
                original = struct.unpack_from('<H', exe, offset + index - 1)[0]
                live = struct.unpack_from('<H', memory, address + index - 1)[0]
                if (live - original) & 0xffff != relocation:
                    raise ValueError('resident action far-call relocation differs')
            mapped['native_code'] = dict(filename='GATEMAIN.EXE', file_offset=offset)
        result.append(mapped)
    if len(deltas) != 1:
        raise ValueError('resident code segment does not have a single EXE offset base')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('snapshot', type=pathlib.Path, help='probe dump prefix, without .bin')
    parser.add_argument('--action', action='append', type=lambda s: int(s, 0),
                        help='action ID (repeatable; defaults to observed commands)')
    parser.add_argument('--all', action='store_true', help='show all 195 native actions')
    parser.add_argument('--overlay-map', type=pathlib.Path,
                        help='gateway-overlay JSON manifest for original file offsets')
    parser.add_argument('--resident-exe', type=pathlib.Path,
                        help='verify four direct resident actions against GATEMAIN.EXE; '
                             'requires --all --overlay-map')
    parser.add_argument('--format', choices=('text', 'json'), default='text')
    args = parser.parse_args()
    if args.action and args.all:
        parser.error('--action and --all are mutually exclusive')
    if args.resident_exe and (not args.all or not args.overlay_map):
        parser.error('--resident-exe requires --all --overlay-map')
    registers = dict(line.split('=', 1) for line in
                     args.snapshot.with_suffix('.regs').read_text().splitlines())
    ids = range(1, ACTION_COUNT + 1) if args.all else (args.action or KNOWN_ACTIONS.keys())
    records = inspect_actions(args.snapshot.with_suffix('.bin').read_bytes(),
                              int(registers['DS'], 16), ids)
    if args.overlay_map:
        manifest = json.loads(args.overlay_map.read_text())
        records = map_action_entries(records, manifest)
        if args.resident_exe:
            records = map_resident_actions(records,
                                           args.snapshot.with_suffix('.bin').read_bytes(),
                                           args.resident_exe.read_bytes(),
                                           manifest['relocation'])
    if args.format == 'json':
        print(json.dumps(dict(schema_version=1, action_count=len(records), actions=records),
                         indent=2))
        return
    print('action  table_pointer  selector trailer  reference / original code offset  observed_input')
    for record in records:
        action = record['action']
        print(f"0x{action:02x}    {record['handler_segment']:04x}:"
              f"{record['handler_offset']:04x}     {record['selector']:02x}       "
              f"{record['trailer']:02x}    ", end='')
        ref = record.get('rtlink_reference')
        if ref:
            print(f"section={ref['section']} target={ref['target_segment']:04x}:"
                  f"{ref['target_offset']:04x}", end='')
        if 'native_code' in record:
            print(f" file={record['native_code']['filename']}:"
                  f"0x{record['native_code']['file_offset']:x}", end='')
        print(f"  {KNOWN_ACTIONS.get(action, '')}", end='')
        if callback := record.get('selector_callback'):
            print(f"  callback={callback['segment']:04x}:{callback['offset']:04x}", end='')
        if selector_code := record.get('selector_native_code'):
            print(f" callback-file={selector_code['filename']}:"
                  f"0x{selector_code['file_offset']:x}", end='')
        print()
    print('Nonresident action pointers address executable RTLink trampolines; '
          'the selector and trailer bytes have not been assigned semantics.')


if __name__ == '__main__':
    main()
