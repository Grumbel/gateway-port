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

    def visit(parent, depth, ancestry, *, show_heading=True):
        record = by_id[parent]
        padding = '  ' * (depth * 2)
        if show_heading:
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
                        visit(child, depth + 1, ancestry | {child}, show_heading=False)
                # Types other than 2/7/8 have no next-sibling accessor in
                # 0157:09e4, so their list ends at this record.
                child = linked.get('next_sibling', 0)
    visit(container, 0, {container})
    output.append('Child heads and next-sibling pointers reflect one snapshot; '
                  'they are not parser visibility or verb acceptance.')
    return '\n'.join(output)


def audit_chains(objects):
    """Audit all decoded child chains without treating locations as edges.

    Unexpected pointers are reported, not followed indefinitely; this is an
    exploratory audit of *known* sibling accessors, not a parser visibility
    or containment-completeness proof for all native record types.
    """
    by_id = {record['object']: record for record in objects}
    if len(by_id) != len(objects) or not all(0 < object_id <= 0xffff for object_id in by_id):
        raise ValueError('duplicate or invalid object IDs in exported state')
    issues = []
    references = {}
    nonzero_heads = 0
    all_links = 0
    undecoded_tails = 0

    for parent in objects:
        parent_id = parent['object']
        for slot, head in enumerate(parent.get('child_links', [])):
            if not head:
                continue
            nonzero_heads += 1
            seen = set()
            child = head
            while child:
                if child in seen:
                    issues.append(f'0x{parent_id:03x}[{slot}]: sibling cycle at 0x{child:03x}')
                    break
                seen.add(child)
                record = by_id.get(child)
                if record is None:
                    issues.append(f'0x{parent_id:03x}[{slot}]: missing child 0x{child:03x}')
                    break
                all_links += 1
                references.setdefault(child, []).append((parent_id, slot))
                if 'location' in record and record['location'] != parent_id:
                    issues.append(f'0x{child:03x}: location 0x{record["location"]:03x} '
                                  f'!= parent 0x{parent_id:03x}')
                if 'child_slot' in record and record['child_slot'] != slot:
                    issues.append(f'0x{child:03x}: child slot {record["child_slot"]} '
                                  f'!= parent slot {slot}')
                if 'next_sibling' not in record:
                    undecoded_tails += 1
                    break
                child = record['next_sibling']

    for child, locations in references.items():
        unique = set(locations)
        if len(unique) > 1:
            # Some type-1 heads alias the same word for multiple descriptor
            # slots. Do not mistake that known native behavior for a double link.
            if len({parent for parent, _ in unique}) == 1 and all(
                    by_id[parent]['type'] == 1 for parent, _ in unique):
                continue
            issues.append(f'0x{child:03x}: referenced by multiple heads/chains '
                          + ', '.join(f'0x{parent:03x}[{slot}]'
                                      for parent, slot in sorted(unique)))

    output = [f'Native object records: {len(objects)}',
              f'Nonzero indexed child heads: {nonzero_heads}',
              f'Child links traversed: {all_links} ({len(references)} distinct objects)',
              f'Chains ending at a record without a decoded sibling accessor: '
              f'{undecoded_tails}',
              f'Consistency findings: {len(issues)}']
    output.extend(f'  {issue}' for issue in issues[:30])
    if len(issues) > 30:
        output.append(f'  ... {len(issues) - 30} more findings')
    output.append('Audit checks known child-head/sibling metadata only; a clean '
                  'result does not prove complete engine semantics or parser visibility.')
    return '\n'.join(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('objects_json', type=Path,
                        help='inspect_object_state.py output (not raw .bin)')
    selection = parser.add_mutually_exclusive_group(required=True)
    selection.add_argument('--container', type=lambda text: int(text, 0),
                           help='native container object ID, e.g. 0x124')
    selection.add_argument('--audit', action='store_true',
                           help='check all decoded child chains for inconsistent pointers')
    parser.add_argument('--recursive', action='store_true',
                        help='also expand nested child containers')
    parser.add_argument('--max-depth', type=int, default=3,
                        help='maximum nested container depth (default 3)')
    args = parser.parse_args()
    try:
        data = json.loads(args.objects_json.read_text())
        if args.audit:
            print(audit_chains(data['objects']))
        else:
            print(resolve_chains(data['objects'], args.container,
                                 recursive=args.recursive, max_depth=args.max_depth))
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
