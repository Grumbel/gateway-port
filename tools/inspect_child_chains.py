#!/usr/bin/env python3
"""Expand indexed Gateway child-head words into native sibling chains.

Input is JSON exported by inspect_object_state.py from a native probe dump.
This is structural containment evidence, not parser visibility or reachability.
"""
import argparse
import json
from pathlib import Path


def resolve_chains(objects, container, *, recursive=False, max_depth=3):
    if not 0 < container <= 0xffff or not 0 <= max_depth <= 32:
        raise ValueError('container and maximum depth are out of range')
    by_id = {entry['object']: entry for entry in objects}
    if len(by_id) != len(objects) or not all(0 < key <= 0xffff for key in by_id):
        raise ValueError('duplicate or invalid object IDs in exported state')
    if container not in by_id:
        raise ValueError(f'object 0x{container:03x} absent from exported state')
    output = []

    def name(entry):
        return str(entry.get('name', '')).replace('\n', ' ').replace('\t', ' ').strip()

    def describe(entry):
        title = f'0x{entry["object"]:03x}'
        if name(entry):
            title += f' {name(entry)}'
        return f'{title} (type {entry["type"]})'

    def visit(parent, depth, ancestry):
        record = by_id[parent]
        padding = '  ' * (depth * 2)
        output.append(f'{padding}{describe(record)} child slots: '
                      f'{len(record.get("child_links", []))}')
        if depth > max_depth:
            return
        for slot, child_head in enumerate(record.get('child_links', [])):
            output.append(f'{padding}  slot {slot}:')
            seen = set()
            child = child_head
            if not child:
                output.append(f'{padding}    (empty)')
            while child:
                if child in seen:
                    raise ValueError(f'cycle in sibling chain of 0x{parent:03x}, slot {slot}')
                seen.add(child)
                linked = by_id.get(child)
                if linked is None:
                    raise ValueError(f'dangling child 0x{child:03x} '
                                     f'of 0x{parent:03x}, slot {slot}')
                notes = []
                if 'location' in linked and linked['location'] != parent:
                    notes.append(f'location=0x{linked["location"]:03x} != parent')
                if 'child_slot' in linked and linked['child_slot'] != slot:
                    notes.append(f'own_slot={linked["child_slot"]} != {slot}')
                suffix = f' [{"; ".join(notes)}]' if notes else ''
                output.append(f'{padding}    {describe(linked)}{suffix}')
                if recursive and depth < max_depth:
                    if child in ancestry:
                        output.append(f'{padding}      (recursive containment cycle)')
                    elif any(linked.get('child_links', [])):
                        visit(child, depth + 1, ancestry | {child})
                # Types other than 2/7/8 have no next-sibling accessor in
                # 0157:09e4, so their list ends at this record.
                child = linked.get('next_sibling', 0)
    visit(container, 0, {container})
    output.append('Child heads and next-sibling pointers reflect one snapshot; '
                  'they are not parser visibility or verb acceptance.')
    return '\n'.join(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('objects_json', type=Path,
                        help='inspect_object_state.py output (not raw .bin)')
    parser.add_argument('--container', type=lambda text: int(text, 0), required=True,
                        help='native container object ID, e.g. 0x124')
    parser.add_argument('--recursive', action='store_true',
                        help='also expand nested child containers')
    parser.add_argument('--max-depth', type=int, default=3,
                        help='maximum nested container depth (default 3)')
    args = parser.parse_args()
    try:
        data = json.loads(args.objects_json.read_text())
        print(resolve_chains(data['objects'], args.container,
                             recursive=args.recursive, max_depth=args.max_depth))
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
