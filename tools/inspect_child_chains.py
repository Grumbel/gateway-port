#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Expand indexed Gateway child-head words into native sibling chains.

Input is JSON exported by inspect_object_state.py from a native probe dump.
This is structural containment evidence, not parser visibility or reachability.
"""
import argparse
from collections import Counter
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


def audit_chains(objects, *, list_unlinked=False, unlinked_type=None, limit=40):
    """Audit all decoded child chains without treating locations as edges.

    Unexpected pointers are reported, not followed indefinitely; this is an
    exploratory audit of *known* sibling accessors, not a parser visibility
    or containment-completeness proof for all native record types.
    """
    if unlinked_type is not None and not 1 <= unlinked_type <= 8:
        raise ValueError('unlinked type must be a native object type from 1 to 8')
    if not 0 <= limit <= 734:
        raise ValueError('unlinked listing limit must be between 0 and 734')
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

    # A child can only have one owning *object*, though the type-1 accessor
    # may expose the same head through multiple descriptor slots. Build this
    # graph from observed child links, never by guessing from location words.
    owners = {}
    for child, locations in references.items():
        parents = {parent for parent, _ in locations}
        if len(parents) == 1:
            owners[child] = next(iter(parents))

    def ancestry(child):
        path = []
        seen = {}
        cursor = child
        while cursor in owners:
            if cursor in seen:
                return None, len(path), path[seen[cursor]:]
            seen[cursor] = len(path)
            path.append(cursor)
            cursor = owners[cursor]
        return cursor, len(path), []

    roots = Counter()
    max_depth = 0
    cycles = set()
    for child in sorted(references):
        root, depth, cycle = ancestry(child)
        if cycle:
            rotations = [tuple(cycle[index:] + cycle[:index])
                         for index in range(len(cycle))]
            cycles.add(min(rotations))
            continue
        if root is not None:
            roots[root] += 1
            max_depth = max(max_depth, depth)
    for cycle in sorted(cycles):
        issues.append('nested containment cycle: ' + ' -> '.join(
            f'0x{object_id:03x}' for object_id in (*cycle, cycle[0])))

    unlinked = [record for record in objects if record['object'] not in references]

    def name(record):
        return str(record.get('name', '')).replace('\n', ' ').replace('\t', ' ').strip()

    output = [f'Native object records: {len(objects)}',
              f'Nonzero indexed child heads: {nonzero_heads}',
              f'Child links traversed: {all_links} ({len(references)} distinct objects)',
              f'Chains ending at a record without a decoded sibling accessor: '
              f'{undecoded_tails}',
              f'Consistency findings: {len(issues)}']
    output.extend(f'  {issue}' for issue in issues[:30])
    if len(issues) > 30:
        output.append(f'  ... {len(issues) - 30} more findings')
    output.extend(['', 'Indexed child ownership by native record type:',
                   'type  total  linked  unlinked  unlinked/location!=0',
                   '----  -----  ------  --------  --------------------'])
    for kind in range(1, 9):
        records = [record for record in objects if record['type'] == kind]
        linked = sum(record['object'] in references for record in records)
        located = sum(record['object'] not in references and record.get('location', 0) != 0
                      for record in records)
        output.append(f'{kind:4}  {len(records):5}  {linked:6}  '
                      f'{len(records) - linked:8}  {located:20}')
    output.extend(['', f'Unlinked records (not reached from any indexed head): {len(unlinked)}',
                   f'Ownership roots with referenced descendants: {len(roots)}',
                   f'Maximum decoded nesting depth: {max_depth}',
                   'Largest ownership roots (distinct descendants):'])
    for object_id, descendants in sorted(roots.items(), key=lambda pair: (-pair[1], pair[0]))[:10]:
        record = by_id[object_id]
        output.append(f'  0x{object_id:03x} {name(record)[:40]} '
                      f'(type {record["type"]}): {descendants}')
    if not roots:
        output.append('  (none; inspect any reported cycles or shared parents)')
    if list_unlinked:
        selected = [record for record in sorted(unlinked, key=lambda r: (r['type'], r['object']))
                    if unlinked_type is None or record['type'] == unlinked_type]
        output.append(f'Unlinked object details (type '
                      f'{unlinked_type if unlinked_type is not None else "all"}): '
                      f'{len(selected)} records, showing at most {limit}')
        for record in selected[:limit]:
            location = (f'0x{record["location"]:03x}' if 'location' in record else '?')
            heads = sum(bool(head) for head in record.get('child_links', []))
            output.append(f'  0x{record["object"]:03x} type={record["type"]} '
                          f'location={location} nonzero-heads={heads} {name(record)}')
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
    parser.add_argument('--list-unlinked', action='store_true',
                        help='with --audit, list objects not reached from a child head')
    parser.add_argument('--unlinked-type', type=int,
                        help='with --list-unlinked, show only native records of this type (1..8)')
    parser.add_argument('--limit', type=int, default=40,
                        help='maximum unlinked records printed (default 40)')
    args = parser.parse_args()
    try:
        if (args.list_unlinked or args.unlinked_type is not None or args.limit != 40) and not args.audit:
            parser.error('--list-unlinked, --unlinked-type and --limit require --audit')
        if args.unlinked_type is not None and not args.list_unlinked:
            parser.error('--unlinked-type requires --list-unlinked')
        data = json.loads(args.objects_json.read_text())
        if args.audit:
            print(audit_chains(data['objects'], list_unlinked=args.list_unlinked,
                               unlinked_type=args.unlinked_type, limit=args.limit))
        else:
            print(resolve_chains(data['objects'], args.container,
                                 recursive=args.recursive, max_depth=args.max_depth))
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
