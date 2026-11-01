#!/usr/bin/env python3
"""Summarize native Gateway room/drawer calls, including older unannotated traces."""
import argparse
from collections import Counter, defaultdict
import pathlib
import re


ENTRY = re.compile(r'^entry ms=(\d+) pc=([0-9a-f]+) cs=([0-9a-f]+) .*\bstack=([^ ]+)')
KEY = re.compile(r'^key ms=(\d+) value=([0-9a-f]+)(?:\s|$)')
FIELDS = re.compile(r'\bstory_(object|event|command)=([0-9a-f]+)(?:\s|$)')
HANDLERS = {0x49db5: 0xee, 0x4a378: 0x124}


def collect(lines):
    """Return (ms, object, event, command_or_None, recent_input) records."""
    typed = []
    last_enter = -1
    last_input = ''
    results = []
    for line in lines:
        key = KEY.match(line)
        if key:
            ms, value = int(key[1]), int(key[2], 16)
            char = value & 0xff
            if char == 13:
                last_input = ''.join(typed).strip()
                last_enter = ms
                typed = []
            elif char == 27:
                typed = []
            elif 32 <= char <= 126:
                typed.append(chr(char))
            continue
        entry = ENTRY.match(line)
        if not entry:
            continue
        ms, pc, cs = int(entry[1]), int(entry[2], 16), int(entry[3], 16)
        # In the original Gateway drawer handler, 49d8:0610 (linear
        # 0x4a390) is `cmp ax,0x61`, immediately after loading the parser
        # command byte into AL and zeroing AH.  This code hook recovers the
        # actual command register value without guessing a DS pointer.
        if pc == 0x4a390 and cs == 0x49d8 and results:
            previous = results[-1]
            command = re.search(r'\bax=([0-9a-f]+)(?:\s|$)', line)
            if (command and previous[1] == 0x124 and
                    0 <= ms - previous[0] <= 100):
                results[-1] = (previous[0], previous[1], previous[2],
                               int(command[1], 16), previous[4])
            continue
        if cs != 0x49d8 or pc not in HANDLERS:
            continue
        fields = {name: int(value, 16) for name, value in FIELDS.findall(line)}
        if fields.get('object', HANDLERS[pc]) != HANDLERS[pc]:
            continue
        # Older entry hooks dumped the far return IP/CS and argument words,
        # but not the story_* annotation.  The event is the third word at
        # [SS:SP+4], so preserve it even without the command byte.
        if 'event' in fields:
            event = fields['event']
        else:
            stack = entry[4].split(',')
            if len(stack) < 3:
                continue
            event = int(stack[2], 16)
        recent = last_input if last_enter >= 0 and 0 <= ms - last_enter <= 2000 else ''
        results.append((ms, HANDLERS[pc], event, fields.get('command'), recent))
    return results


def summarize(lines):
    records = collect(lines)
    counts = Counter((obj, event, cmd) for _, obj, event, cmd, _ in records)
    first = {}
    inputs = defaultdict(set)
    for ms, obj, event, cmd, recent in records:
        key = obj, event, cmd
        first.setdefault(key, ms)
        if recent:
            inputs[key].add(recent)
    return [(obj, event, cmd, counts[(obj, event, cmd)], first[(obj, event, cmd)],
             tuple(sorted(inputs[(obj, event, cmd)])))
            for obj, event, cmd in sorted(counts, key=lambda k: (k[0], k[1],
                                                                 -1 if k[2] is None else k[2]))]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=pathlib.Path)
    parser.add_argument('--details', action='store_true', help='list individual handler calls')
    args = parser.parse_args()
    lines = args.trace.read_text().splitlines()
    rows = summarize(lines)
    if not rows:
        parser.error('no Gateway room/drawer entries; check trace-entry addresses')
    print('object event command calls first_ms recent_input')
    for obj, event, cmd, count, ms, inputs in rows:
        code = f'0x{cmd:02x}' if cmd is not None else '  ? '
        print(f'0x{obj:03x}  0x{event:02x}  {code}    {count:5}  {ms:8}  {", ".join(inputs)}')
    if args.details:
        print('\nIndividual entries (input is nearest recent Enter, not a proven action mapping):')
        for ms, obj, event, cmd, input_text in collect(lines):
            code = f'0x{cmd:02x}' if cmd is not None else '?'
            print(f'  ms={ms:6} object=0x{obj:03x} event=0x{event:02x}'
                  f' command={code} recent_input={input_text!r}')
    if any(row[2] is None for row in rows):
        print('Command byte unavailable for some entries; event IDs recovered from the far-call stack.')
    print('Recent input is a timing association, not proof of the parser command mapping.')


if __name__ == '__main__':
    main()
