#!/usr/bin/env python3
"""Export one Gateway object snapshot as a typed, ordered containment forest.

Reads inspect_object_state.py JSON; no executable assets or guest execution.
Type-1 roots are *room candidates*. This snapshot is not a recovered script
database, navigation map, renderer, or proof of object visibility.
"""
import argparse
from collections import Counter
import json
from pathlib import Path


def export_world(objects):
    if not isinstance(objects, list):
        raise ValueError('expected an object-record list')
    by_id = {}
    for record in objects:
        object_id = record['object']
        if (not isinstance(object_id, int) or isinstance(object_id, bool) or
                not 0 < object_id <= 0xffff or object_id in by_id):
            raise ValueError('duplicate or invalid object ID')
        if record['type'] not in range(1, 9):
            raise ValueError(f'object 0x{object_id:03x}: invalid native type')
        by_id[object_id] = record

    owners = {}
    child_lists = {}
    for parent_id, record in sorted(by_id.items()):
        slots = []
        aliased_heads = {}
        for slot, head in enumerate(record.get('child_links', [])):
            if not isinstance(head, int) or not 0 <= head <= 0xffff:
                raise ValueError(f'object 0x{parent_id:03x}: invalid child head')
            # Type-1's original accessor repeats the same fixed word for
            # multiple advertised slots. Represent the additional slots as
            # aliases rather than duplicate the same objects in the tree.
            if head and record['type'] == 1 and head in aliased_heads:
                slots.append(dict(slot=slot, alias_of=aliased_heads[head], children=[]))
                continue
            if head:
                aliased_heads[head] = slot
            chain = []
            seen = set()
            child = head
            while child:
                if child in seen:
                    raise ValueError(f'object 0x{parent_id:03x} slot {slot}: sibling cycle')
                seen.add(child)
                linked = by_id.get(child)
                if linked is None:
                    raise ValueError(f'object 0x{parent_id:03x} slot {slot}: '
                                     f'dangling child 0x{child:03x}')
                if child in owners:
                    raise ValueError(f'object 0x{child:03x}: multiple indexed owners')
                if linked.get('location', parent_id) != parent_id:
                    raise ValueError(f'object 0x{child:03x}: location differs from '
                                     f'parent 0x{parent_id:03x}')
                if linked.get('child_slot', slot) != slot:
                    raise ValueError(f'object 0x{child:03x}: slot differs from '
                                     f'parent 0x{parent_id:03x} slot {slot}')
                if 'next_sibling' not in linked:
                    raise ValueError(f'object 0x{child:03x}: sibling accessor not decoded')
                owners[child] = (parent_id, slot)
                chain.append(child)
                child = linked['next_sibling']
            slots.append(dict(slot=slot, children=chain))
        child_lists[parent_id] = slots

    roots = sorted(set(by_id) - set(owners))
    visited = set()
    max_depth = 0

    def node(object_id, ancestry, depth):
        nonlocal max_depth
        if object_id in ancestry:
            raise ValueError(f'object 0x{object_id:03x}: nested containment cycle')
        if object_id in visited:
            raise ValueError(f'object 0x{object_id:03x}: shared nested containment')
        visited.add(object_id)
        max_depth = max(max_depth, depth)
        record = by_id[object_id]
        result = dict(id=object_id, native_type=record['type'], name=record.get('name', ''),
                      declared_child_slots=record.get('child_count',
                                                      len(record.get('child_links', []))))
        for source, target in [('secondary_name', 'secondary_name'),
                               ('handler', 'native_handler'),
                               ('location', 'native_location'),
                               ('child_slot', 'native_parent_slot'),
                               ('next_sibling', 'native_next_sibling'),
                               ('relation_count', 'native_relation_count'),
                               ('relation_links', 'native_relation_links'),
                               ('native_code', 'native_code')]:
            if source in record:
                result[target] = record[source]
        next_ancestry = ancestry | {object_id}
        slots = []
        descendants = 0
        for source_slot in child_lists[object_id]:
            exported_slot = dict(slot=source_slot['slot'])
            if 'alias_of' in source_slot:
                exported_slot['alias_of'] = source_slot['alias_of']
            children = [node(child, next_ancestry, depth + 1)
                        for child in source_slot['children']]
            exported_slot['objects'] = children
            slots.append(exported_slot)
            descendants += sum(1 + child['descendant_count'] for child in children)
        result['child_slots'] = slots
        result['descendant_count'] = descendants
        return result

    forest = [node(object_id, set(), 0) for object_id in roots]
    if len(visited) != len(by_id):
        # With unique ownership, anything not reached from a root belongs to
        # a cycle of parent pointers. Never silently omit such objects.
        missing = min(set(by_id) - visited)
        raise ValueError(f'object 0x{missing:03x}: no containment root (cycle)')
    by_type = Counter(record['type'] for record in objects)
    root_type = Counter(by_id[object_id]['type'] for object_id in roots)
    room_roots = [root for root in forest if root['native_type'] == 1]
    room_direct_type8 = [sum(child['native_type'] == 8
                             for slot in room['child_slots'] for child in slot['objects'])
                         for room in room_roots]
    unattached_with_location = [
        dict(id=object_id, name=by_id[object_id].get('name', ''),
             native_type=by_id[object_id]['type'],
             native_location=by_id[object_id]['location'])
        for object_id in roots if by_id[object_id].get('location', 0)
    ]
    connector_candidates = []
    for object_id, record in sorted(by_id.items()):
        if record['type'] != 6:
            continue
        original_links = record.get('relation_links')
        count = record.get('relation_count')
        if original_links is not None and count is not None and len(original_links) != count:
            raise ValueError(f'object 0x{object_id:03x}: type-6 relation count mismatch')
        relations = []
        for slot, target in enumerate(original_links or []):
            if not isinstance(target, int) or not 0 <= target <= 0xffff:
                raise ValueError(f'object 0x{object_id:03x}: invalid relation word')
            referenced = by_id.get(target)
            item = dict(slot=slot, id=target)
            if referenced is not None:
                item['name'] = referenced.get('name', '')
                item['native_type'] = referenced['type']
            relations.append(item)
        rooms = [item['id'] for item in relations if item.get('native_type') == 1]
        nonzero = [item['id'] for item in relations if item['id']]
        pair = (rooms if len(rooms) == 2 and len(set(rooms)) == 2 and
                len(nonzero) == 2 else [])
        location = record.get('location', 0)
        location_reference = dict(id=location)
        if location in by_id:
            location_reference.update(name=by_id[location].get('name', ''),
                                      native_type=by_id[location]['type'])
        connector_candidates.append(dict(
            id=object_id, name=record.get('name', ''),
            native_handler=record.get('handler', 0),
            native_location=location,
            native_location_reference=location_reference,
            declared_relation_slots=count,
            relations_available=original_links is not None,
            relations=relations,
            two_room_candidate=pair,
        ))
    return dict(
        schema_version=2,
        meaning='single Gateway native object-state snapshot; no scripts or parser visibility',
        counts=dict(records=len(objects), indexed_children=len(owners), roots=len(roots),
                    room_candidates=root_type[1], populated_roots=sum(
                        bool(record['descendant_count']) for record in forest),
                    empty_room_candidates=sum(not room['descendant_count']
                                              for room in room_roots),
                    rooms_with_direct_type8=sum(bool(count) for count in room_direct_type8),
                    direct_type8_room_children=sum(room_direct_type8),
                    max_depth=max_depth,
                    type6_connector_candidates=len(connector_candidates),
                    two_room_relation_candidates=sum(
                        bool(candidate['two_room_candidate']) for candidate in connector_candidates),
                    by_native_type={str(kind): by_type[kind] for kind in range(1, 9)}),
        room_candidate_ids=[object_id for object_id in roots if by_id[object_id]['type'] == 1],
        unattached_with_location=unattached_with_location,
        connector_candidates=connector_candidates,
        roots=forest,
    )


def markdown_world(world):
    counts = world['counts']
    lines = [
        '# Gateway native object snapshot',
        '',
        f'{counts["records"]} object records; {counts["indexed_children"]} indexed children; '
        f'{counts["roots"]} roots. Maximum nesting depth: {counts["max_depth"]}.',
        '',
        '## Type-1 room candidates',
        '',
        '| Object | Name | Direct contents | Direct type-8 | All descendants |',
        '| --- | --- | ---: | ---: | ---: |',
    ]

    def label(node):
        return str(node.get('name', '')).replace('|', '\\|').replace('\n', ' ')

    for root in world['roots']:
        if root['native_type'] != 1:
            continue
        direct = sum(len(slot['objects']) for slot in root['child_slots'])
        scenery = sum(child['native_type'] == 8 for slot in root['child_slots']
                      for child in slot['objects'])
        lines.append(f'| `0x{root["id"]:03x}` | {label(root)} | {direct} | {scenery} | '
                     f'{root["descendant_count"]} |')
    lines.extend(['', '## Other populated roots', '',
                  '| Object | Type | Name | All descendants |',
                  '| --- | ---: | --- | ---: |'])
    for root in world['roots']:
        if root['native_type'] == 1 or not root['descendant_count']:
            continue
        lines.append(f'| `0x{root["id"]:03x}` | {root["native_type"]} | '
                     f'{label(root)} | {root["descendant_count"]} |')
    lines.extend(['', '## Unattached records with nonzero direct location', '',
                  '| Object | Type | Name | Location word |',
                  '| --- | ---: | --- | --- |'])
    for record in world['unattached_with_location']:
        lines.append(f'| `0x{record["id"]:03x}` | {record["native_type"]} | '
                     f'{label(record)} | `0x{record["native_location"]:03x}` |')
    lines.extend(['', '## Type-6 connector / multi-relation candidates', '',
                  '| Object | Name | Direct location word | Original relation words | '
                  'Two-room candidate |',
                  '| --- | --- | --- | --- | --- |'])
    for candidate in world['connector_candidates']:
        location = candidate['native_location_reference']
        location_text = f'`0x{location["id"]:03x}`'
        if 'native_type' in location:
            location_text += f' {label(location)} (type {location["native_type"]})'
        if not candidate['relations_available']:
            resolved = '(not exported; regenerate from inspect_object_state.py JSON)'
        else:
            labels = []
            for entry in candidate['relations']:
                text = f'`0x{entry["id"]:03x}`'
                if 'native_type' in entry:
                    text += f' {label(entry)} (type {entry["native_type"]})'
                labels.append(text)
            resolved = ', '.join(labels) if labels else '(no relation words)'
        pair = ', '.join(f'`0x{room:03x}`' for room in candidate['two_room_candidate']) or '—'
        lines.append(f'| `0x{candidate["id"]:03x}` | {label(candidate)} | '
                     f'{location_text} | '
                     f'{resolved} | {pair} |')
    lines.extend(['', 'This is one captured native state. Type-1 records are '
                  '*room candidates*, not proven scene or navigation definitions. '
                  'Unattached objects may be inactive or use other native semantics; '
                  'Type-6 words are original relation fields, not confirmed exits or '
                  'directed travel paths. Nothing here establishes visibility, '
                  'scripting, or traversability.'])
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('objects_json', type=Path,
                        help='inspect_object_state.py JSON output')
    parser.add_argument('--format', choices=('json', 'markdown'), default='markdown',
                        help='JSON forest for tools or room inventory Markdown (default)')
    args = parser.parse_args()
    try:
        data = json.loads(args.objects_json.read_text())
        world = export_world(data['objects'])
        if args.format == 'json':
            print(json.dumps(world, indent=2, ensure_ascii=False))
        else:
            print(markdown_world(world), end='')
    except (KeyError, TypeError, OSError, ValueError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
