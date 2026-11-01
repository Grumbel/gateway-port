#!/usr/bin/env python3
"""Group verified Gateway room/drawer entry traces by raw command/event ID."""
import argparse
from collections import Counter
import pathlib
import re


ENTRY = re.compile(r'^entry ms=(\d+) .*\bstory_object=([0-9a-f]+) '
                   r'story_event=([0-9a-f]+) story_command=([0-9a-f]+)(?:\s|$)')


def summarize(lines):
    counts = Counter()
    first = {}
    for line in lines:
        match = ENTRY.match(line)
        if not match:
            continue
        ms = int(match[1], 10)
        key = tuple(int(match[i], 16) for i in (2, 3, 4))
        counts[key] += 1
        first.setdefault(key, ms)
    return [(obj, event, cmd, count, first[(obj, event, cmd)])
            for (obj, event, cmd), count in sorted(counts.items())]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=pathlib.Path)
    args = parser.parse_args()
    rows = summarize(args.trace.read_text().splitlines())
    if not rows:
        parser.error('no annotated story entries; check overlay address/layout and trace hooks')
    print('object event command calls first_ms')
    for obj, event, cmd, count, ms in rows:
        print(f'0x{obj:03x}  0x{event:02x}  0x{cmd:02x}    {count:5}  {ms}')
    print('Command IDs are raw parser state, not yet verified verb names.')


if __name__ == '__main__':
    main()
