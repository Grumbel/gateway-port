#!/usr/bin/env python3
"""Export Gateway object state and native handler tables from a probe snapshot."""
import argparse
import json
import pathlib
import struct

HANDLER_FIELDS = [0x10, 0x22, 0x0a, 0x12, 0x14, 0x16, 0x1e, 0x16]
LOCATION_FIELDS = {2: 0x0e, 6: 0x0a, 7: 0x0c, 8: 0x0c}


def relation_links(kind, count, raw):
    """Decode the ordered links traversed by EXE 0157:00c5 via 0157:05c1.

    Kind 6 stores count consecutive words at +0x0a; kinds 2, 7, and 8
    repeat their single location word for each requested link slot. Other
    object kinds have no link accessor, even if the descriptor count is set.
    """
    if kind not in LOCATION_FIELDS or not count:
        return []
    if kind == 6:
        if 0x0a + 2 * count > len(raw):
            raise ValueError('type 6 relation list exceeds object record')
        return list(struct.unpack_from('<' + 'H' * count, raw, 0x0a))
    location = struct.unpack_from('<H', raw, LOCATION_FIELDS[kind])[0]
    return [location] * count


def inspect_objects(memory, ds, names=None):
    def chunk(segment, offset, size):
        address = segment * 16 + offset
        if address < 0 or address + size > min(len(memory), 0xa0000):
            raise ValueError('object state access outside conventional memory')
        return memory[address:address + size]

    def word(segment, offset):
        return struct.unpack('<H', chunk(segment, offset & 0xffff, 2))[0]

    def pointer(segment, offset):
        return struct.unpack('<HH', chunk(segment, offset & 0xffff, 4))

    count = word(word(ds, 0x8d54), 0x38dc)
    if not 0 < count < 0x8000:
        raise ValueError('invalid object count for Gateway snapshot')
    directory = word(ds, 0x8d50)
    descriptors = word(ds, 0x8d52)
    result = []
    for object_id in range(1, count + 1):
        record = (0xba + object_id * 6) & 0xffff
        kind = chunk(directory, record, 1)[0]
        if not 1 <= kind <= 8:
            raise ValueError(f'object {object_id}: unsupported type {kind}')
        offset, segment = pointer(directory, record + 2)
        relation_count = chunk(descriptors, kind * 3 - 3, 1)[0]
        size = chunk(descriptors, kind * 3 - 1, 1)[0]
        handler_field = HANDLER_FIELDS[kind - 1]
        if size < handler_field + 2:
            raise ValueError(f'object {object_id}: record too short for its handler field')
        raw = chunk(segment, offset, size)
        handler = word(segment, offset + handler_field)
        if handler > 696:
            raise ValueError(f'object {object_id}: handler index out of range')
        item = dict(object=object_id, type=kind, state_segment=segment, state_offset=offset,
                    size=size, name_token=word(segment, offset),
                    secondary_name_token=word(segment, offset + 2), handler=handler,
                    raw_state=raw.hex(), relation_count=relation_count,
                    relation_links=relation_links(kind, relation_count, raw))
        if names is not None:
            for key, label in [('name_token', 'name'), ('secondary_name_token', 'secondary_name')]:
                if not item[key]:
                    continue
                # Name tokens are byte offsets plus one in OBJECT.DAT's payload.
                start = item[key] - 1
                end = names.find(b'\0', start)
                if (start >= len(names) or end == -1 or (start and names[start - 1] != 0)):
                    raise ValueError(f'object {object_id}: invalid name offset')
                item[label] = names[start:end].decode('cp437')
        if kind in LOCATION_FIELDS:
            item['location'] = word(segment, offset + LOCATION_FIELDS[kind])
        if handler:
            bank, index = divmod(handler - 1, 512)
            table = word(ds, 0x8db4 + bank * 2)
            entry_ip, entry_cs = pointer(table, index * 4)
            item['handler_entry'] = dict(segment=entry_cs, offset=entry_ip)
            stub = chunk(entry_cs, entry_ip, 10)
            if stub[0] == 0xe8 and stub[3] == 0xea:
                ip, cs, section = struct.unpack_from('<HHH', stub, 4)
                item['rtlink_stub'] = dict(target_segment=cs, target_offset=ip,
                                           section_word=section)
        result.append(item)
    return result


def map_handler_entries(objects, manifest):
    sections = {s['id']: s for s in manifest['sections']}
    if len(sections) != len(manifest['sections']):
        raise ValueError('duplicate RTLink section IDs')
    mapped = []
    for obj in objects:
        item = dict(obj)
        if item['handler']:
            if 'rtlink_stub' not in item:
                raise ValueError('handler has no recognized RTLink trampoline')
            stub = item['rtlink_stub']
            section = sections.get(stub['section_word'])
            if section is None:
                raise ValueError('handler section absent from overlay manifest')
            image_offset = ((stub['target_segment'] - section['runtime_segment']) * 16 +
                            stub['target_offset'])
            if not 0 <= image_offset < section['file_bytes']:
                raise ValueError('handler target outside file-backed section bytes')
            item['native_code'] = dict(section=section['id'], filename=section['filename'],
                                       image_offset=image_offset,
                                       file_offset=section['image_file_offset'] + image_offset)
        mapped.append(item)
    return mapped


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('snapshot', type=pathlib.Path, help='probe dump prefix, without .bin')
    parser.add_argument('--names', type=pathlib.Path, help='optional original OBJECT.DAT')
    parser.add_argument('--overlay-map', type=pathlib.Path, help='gateway-overlay JSON manifest')
    args = parser.parse_args()
    regs = dict(line.split('=') for line in args.snapshot.with_suffix('.regs').read_text().splitlines())
    names = None
    if args.names:
        file = args.names.read_bytes()
        if len(file) < 2 or struct.unpack_from('<H', file)[0] != len(file) - 2:
            raise ValueError('invalid OBJECT.DAT size header')
        names = file[2:]
    objects = inspect_objects(args.snapshot.with_suffix('.bin').read_bytes(), int(regs['DS'], 16), names)
    if args.overlay_map:
        objects = map_handler_entries(objects, json.loads(args.overlay_map.read_text()))
    print(json.dumps(dict(objects=objects, object_count=len(objects),
                          handler_count=len({o['handler'] for o in objects if o['handler']})), indent=2))


if __name__ == '__main__':
    main()
