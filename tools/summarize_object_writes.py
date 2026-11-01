#!/usr/bin/env python3
"""Locate native instructions mutating a Gateway object's record.

Consumes an opt-in --trace-object-writes replay. The memory hook captures the
value proposed by a guest instruction *before* Unicorn performs the write;
changed means only that the proposed bytes differ from the pre-write bytes.
"""
import argparse
import json
from pathlib import Path

from story_scenarios import parse_sequence


CHILD_BASE = {1: 0x0c, 2: 0x14, 4: 0x0a, 5: 0x0c, 6: 0x0e, 7: 0x12}
COMMAND_TIMES = [18000, 21000, 25000, 28000, 31000, 34000, 37000]
# Original, unrelocated five-byte direct far CALL to 0157:08fc. RTLink adds
# the program segment to the final word at load time.
CHILD_SETTER_CALL = bytes.fromhex('9afc085701')


def fields(line, prefix):
    if not line.startswith(prefix + ' '):
        return None
    return dict(item.split('=', 1) for item in line.split()[1:] if '=' in item)


def parse(lines):
    watch = None
    writes = []
    for line in lines:
        meta = fields(line, 'object-watch')
        if meta is not None:
            if watch is not None:
                raise ValueError('multiple object-watch entries in one trace')
            watch = {key: int(meta[key], 10 if key == 'ms' else 16)
                     for key in ('ms', 'object', 'type', 'size', 'child_count')}
            watch['at'] = meta['at']
            if not 1 <= watch['type'] <= 8 or watch['size'] <= 0:
                raise ValueError('invalid object-watch native type/size')
            continue
        meta = fields(line, 'object-write')
        if meta is None:
            continue
        if watch is None:
            raise ValueError('object-write appeared before object-watch')
        event = {key: int(meta[key], 10 if key == 'ms' else 16)
                 for key in ('ms', 'object', 'pc', 'offset', 'size')}
        event.update(before=meta['before'], value=meta['value'])
        if event['object'] != watch['object'] or not 0 < event['size'] <= 8 or \
                not 0 <= event['offset'] <= watch['size'] - event['size'] or \
                len(event['before']) != 2 * event['size'] or \
                len(event['value']) != 2 * event['size']:
            raise ValueError('invalid object-write record or watched address')
        try:
            bytes.fromhex(event['before'])
            bytes.fromhex(event['value'])
        except ValueError as error:
            raise ValueError('invalid object-write hexadecimal bytes') from error
        writes.append(event)
    if watch is None:
        raise ValueError('no object-watch entry; use --trace-object-writes in the native replay')
    return watch, writes


def child_setter_calls(lines, watched_object=None):
    calls = []
    for line in lines:
        entry = fields(line, 'entry')
        if entry is None or 'child_set_object' not in entry:
            continue
        if int(entry['pc'], 16) != 0x11e6c or int(entry['cs'], 16) != 0x1157:
            raise ValueError('child setter annotation attached to wrong code address')
        object_id = int(entry['child_set_object'], 16)
        if watched_object is not None and object_id != watched_object:
            continue
        stack = entry.get('stack', '').split(',')
        if len(stack) < 2:
            raise ValueError('indexed-child setter trace lacks far return address')
        return_ip, return_cs = (int(word, 16) for word in stack[:2])
        calls.append(dict(ms=int(entry['ms']), object=object_id,
                          slot=int(entry['child_set_slot'], 16),
                          value=int(entry['child_set_value'], 16),
                          return_address=f'{return_cs:04x}:{return_ip:04x}'))
    return calls


def map_child_setter_return(return_address, manifest, overlay):
    """Map a *direct* far call through overlapping RTLink sections by bytes.

    Several original sections occupy the same live bank. Address arithmetic
    alone is not enough: require the linked call opcode and target before
    claiming a source address. Unrecognized/indirect calls remain unresolved.
    """
    cs_text, ip_text = return_address.split(':')
    linear = int(cs_text, 16) * 16 + int(ip_text, 16)
    matches = []
    for section in manifest['sections']:
        if not section['filename'].upper().endswith('GATEMAIN.OVL'):
            continue
        offset = linear - section['runtime_segment'] * 16
        if not 5 <= offset < section['file_bytes']:
            continue
        file_return = section['image_file_offset'] + offset
        if overlay[file_return - 5:file_return] == CHILD_SETTER_CALL:
            matches.append((section['id'], file_return - 5))
    if len(matches) > 1:
        raise ValueError('child setter callsite is ambiguous across RTLink sections')
    return matches[0] if matches else None


def affected_child_slots(watch, offset, size):
    start = CHILD_BASE.get(watch['type'])
    if start is None:
        return []
    # Type 1 repeats one word even when the descriptor advertises many slots.
    count = 1 if watch['type'] == 1 else watch['child_count']
    slots = []
    for slot in range(count):
        child_start = start + slot * 2
        if offset < child_start + 2 and offset + size > child_start:
            slots.append(slot)
    return slots


def summarize(lines, *, child_only=False, changed_only=True, commands=None,
              manifest=None, overlay=None, all_setters=False):
    lines = list(lines)
    watch, writes = parse(lines)
    if commands is not None and len(commands) != len(COMMAND_TIMES):
        raise ValueError('command labels must cover all seven scripted input slots')
    if (manifest is None) != (overlay is None):
        raise ValueError('RTLink manifest and original overlay must be supplied together')
    output = [f'Object 0x{watch["object"]:03x}: native type {watch["type"]}, '
              f'{watch["size"]} bytes at {watch["at"]}, '
              f'{watch["child_count"]} child slots',
              'ms     recent slot     pc       offset  before -> value   child slot',
              '------  --------------  -------  ------  ----------------  ----------']

    def phase(ms):
        index = next((i for i in range(len(COMMAND_TIMES) - 1, -1, -1)
                      if ms >= COMMAND_TIMES[i]), None)
        return ('startup' if index is None else
                commands[index] if commands else f'{COMMAND_TIMES[index] // 1000}s slot')

    count = 0
    for event in writes:
        changed = event['before'] != event['value']
        slots = affected_child_slots(watch, event['offset'], event['size'])
        if (changed_only and not changed) or (child_only and not slots):
            continue
        label = phase(event['ms'])
        # A memory hook fires *before* the write, so this is a proposed value,
        # not an independently verified post-write state.
        field = ','.join(str(slot) for slot in slots) if slots else '-'
        output.append(f'{event["ms"]:6}  {label[:14]:<14}  '
                      f'{event["pc"]:07x}  {event["offset"]:04x}    '
                      f'{event["before"]} -> {event["value"]}  {field}')
        count += 1
    if not count:
        output.append('(no matching writes)')
    header = 'ms     recent slot     object  slot  child  returns to'
    if manifest is not None:
        header += '  original direct call'
    output.extend(['', 'Native indexed-child setter calls (0157:08fc):',
                   header, '------  --------------  ------  ----  -----  ----------'])
    setters = child_setter_calls(lines, None if all_setters else watch['object'])
    for call in setters:
        entry = (f'{call["ms"]:6}  {phase(call["ms"])[:14]:<14}  '
                 f'0x{call["object"]:03x}  {call["slot"]:4}  0x{call["value"]:03x}  '
                 f'{call["return_address"]}')
        if manifest is not None:
            location = map_child_setter_return(call['return_address'], manifest, overlay)
            entry += (f'  section={location[0]} GATEMAIN.OVL:0x{location[1]:x}'
                      if location else '  unresolved (no matching direct call)')
        output.append(entry)
    if not setters:
        output.append('(none observed)')
    output.append('These are guest memory-write attempts before execution, not independently '
                  'verified changes. Slot labels identify record offsets only, not parser success.')
    return '\n'.join(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--child-only', action='store_true',
                        help='show writes overlapping parser child-link words only')
    parser.add_argument('--all', action='store_true',
                        help='also show writes that repeat existing bytes')
    parser.add_argument('--commands', help='seven comma-separated replay input labels')
    parser.add_argument('--overlay-map', type=Path,
                        help='gateway-overlay JSON manifest (used with --overlay-file)')
    parser.add_argument('--overlay-file', type=Path,
                        help='original GATEMAIN.OVL bytes for callsite signature checks')
    parser.add_argument('--all-setters', action='store_true',
                        help='include native child setter calls for all object IDs in this trace')
    args = parser.parse_args()
    try:
        commands = parse_sequence(args.commands) if args.commands is not None else None
        if bool(args.overlay_map) != bool(args.overlay_file):
            parser.error('--overlay-map and --overlay-file must be used together')
        manifest = json.loads(args.overlay_map.read_text()) if args.overlay_map else None
        overlay = args.overlay_file.read_bytes() if args.overlay_file else None
        print(summarize(args.trace.read_text().splitlines(), child_only=args.child_only,
                        changed_only=not args.all, commands=commands,
                        manifest=manifest, overlay=overlay, all_setters=args.all_setters))
    except (OSError, ValueError, KeyError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
