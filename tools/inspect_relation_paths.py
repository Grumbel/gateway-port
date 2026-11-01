#!/usr/bin/env python3
"""Follow native Gateway object-relation links in exported object-state JSON.

This is the cycle-safe counterpart of resident GATEMAIN.EXE 0157:00c5, not
the parser's visibility/permission rule. Export JSON with inspect_object_state.py.
"""

import argparse
import json
from pathlib import Path


def relation_path(objects, source, target):
    """Return one path of >= 1 edges, or None; zero ends a link list."""
    indexed = {obj['object']: obj for obj in objects}
    if len(indexed) != len(objects):
        raise ValueError('duplicate object IDs in state export')
    if source not in indexed:
        raise ValueError(f'unknown source object {source:#x}')
    pending = [(source, [source])]
    visited = {source}
    while pending:
        current, path = pending.pop()
        obj = indexed.get(current)
        if obj is None:
            continue
        for link in obj['relation_links']:
            if not link:  # Native loop exits on the first zero.
                break
            candidate = path + [link]
            if link == target:
                return candidate
            if link not in visited:
                visited.add(link)
                pending.append((link, candidate))
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('snapshot_json', type=Path,
                        help='output of inspect_object_state.py (with relation links)')
    parser.add_argument('--source', type=lambda x: int(x, 0), required=True,
                        help='starting object ID, e.g. 0x123')
    parser.add_argument('--target', type=lambda x: int(x, 0), required=True,
                        help='related object ID, e.g. 0xd3 for the player')
    args = parser.parse_args()
    objects = json.loads(args.snapshot_json.read_text())['objects']
    path = relation_path(objects, args.source, args.target)
    if path is None:
        print(f'no relation path from {args.source:#x} to {args.target:#x}')
        return
    names = {obj['object']: obj.get('name', '') for obj in objects}
    print(' -> '.join(f'{item:#x}' + (f' ({names[item]})' if names.get(item) else '')
                      for item in path))


if __name__ == '__main__':
    main()
