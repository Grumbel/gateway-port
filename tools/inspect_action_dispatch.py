#!/usr/bin/env python3
"""Read Gateway's native 195-entry action table from a live memory dump.

Gateway's resident dispatch code reads a selector byte at 0x27 + 6*action
and a far handler pointer at 0x28 + 6*action. The following byte is separate:
reading a 16-bit "other" word there mixes it with the next action's selector.
A nonresident action's pointer leads to the START of a ten-byte RTLink
trampoline (near CALL to the manager gate, followed by a far JMP and section
ID), not the embedded reference at offset +4.
"""
import argparse
import json
import pathlib
import struct


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
        # These action pointers point to *executable trampoline starts*.
        # The reference is at +4 (after e8 disp16; ea), and the near call
        # resolves to the original Gateway RTLink manager gate at 2c24:058d.
        reference_address = cs * 16 + ip
        if cs == 0x2c24 and reference_address + 10 <= min(len(memory), 0xa0000):
            displacement = struct.unpack_from('<h', memory, reference_address + 1)[0]
            gate = (ip + 3 + displacement) & 0xffff
            if (memory[reference_address] == 0xe8 and
                    memory[reference_address + 3] == 0xea and gate == 0x058d):
                target_ip, target_cs, section_id = struct.unpack_from('<3H', memory,
                                                                       reference_address + 4)
                if section_id >= 58:
                    raise ValueError('invalid section ID in action RTLink trampoline')
                entry['rtlink_reference'] = dict(section=section_id,
                                                 target_segment=target_cs,
                                                 target_offset=target_ip)
        results.append(entry)
    return results


def map_action_entries(entries, manifest):
    sections = {item['id']: item for item in manifest['sections']}
    if len(sections) != len(manifest['sections']):
        raise ValueError('duplicate RTLink section IDs')
    output = []
    for item in entries:
        result = dict(item)
        ref = item.get('rtlink_reference')
        if ref:
            section = sections.get(ref['section'])
            if section is None:
                raise ValueError('action RTLink reference has no section in manifest')
            displacement = (ref['target_segment'] - section['runtime_segment']) * 16
            image_offset = displacement + ref['target_offset']
            if not 0 <= image_offset < section['file_bytes']:
                raise ValueError('action target outside RTLink section file-backed bytes')
            result['native_code'] = dict(filename=section['filename'], section=section['id'],
                                         image_offset=image_offset,
                                         file_offset=section['image_file_offset'] + image_offset)
        output.append(result)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('snapshot', type=pathlib.Path, help='probe dump prefix, without .bin')
    parser.add_argument('--action', action='append', type=lambda s: int(s, 0),
                        help='action ID (repeatable; defaults to observed commands)')
    parser.add_argument('--all', action='store_true', help='show all 195 native actions')
    parser.add_argument('--overlay-map', type=pathlib.Path,
                        help='gateway-overlay JSON manifest for original file offsets')
    args = parser.parse_args()
    if args.action and args.all:
        parser.error('--action and --all are mutually exclusive')
    registers = dict(line.split('=', 1) for line in
                     args.snapshot.with_suffix('.regs').read_text().splitlines())
    ids = range(1, ACTION_COUNT + 1) if args.all else (args.action or KNOWN_ACTIONS.keys())
    records = inspect_actions(args.snapshot.with_suffix('.bin').read_bytes(),
                              int(registers['DS'], 16), ids)
    if args.overlay_map:
        records = map_action_entries(records, json.loads(args.overlay_map.read_text()))
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
        print(f"  {KNOWN_ACTIONS.get(action, '')}")
    print('Nonresident action pointers address executable RTLink trampolines; '
          'the selector and trailer bytes have not been assigned semantics.')


if __name__ == '__main__':
    main()
