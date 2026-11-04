# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Inspect original Gateway 1's conditional 14-byte scene-layer records.

The scene update routine at GATEMAIN.OVL:0x2827 and its per-record predicate
at ovl:0x272c are separate from the ten-byte RGN picker rules. This is a
read-only source inspector, not a replacement for the guest's graphics code.
"""

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

from inspect_region_conditions import (
    EXE_DATA_BASE, EXE_SHA256, callback_file_offset,
    linked_pointer_file_offset, section_by_id, snapshot_memory)


RECORD_SIZE = 14
CALL_TARGET = (0x425b, 0x1c24)


def decode_record(source, at, manifest=None, owner=None):
    obj, relation, picture, prop, extra, callback_ip, callback_cs = \
        struct.unpack_from('<7H', source, at)
    callback = (callback_file_offset(manifest, owner, callback_ip, callback_cs)
                if manifest is not None and owner is not None else None)
    return {
        'object_id': obj, 'relation_id': relation, 'picture_id': picture,
        'property': prop, 'extra_id': extra,
        'callback_offset': callback_ip, 'callback_segment': callback_cs,
        'callback_file': callback, 'file_offset': at,
    }


def inspect(ovl, manifest, section_id, image_offset, count):
    owner = section_by_id(manifest, section_id)
    if count < 1 or count > 4096 or image_offset < 0 or \
       image_offset + RECORD_SIZE * count > owner['file_bytes']:
        raise ValueError('picture condition table extends outside section')
    at = owner['image_file_offset'] + image_offset
    if at + RECORD_SIZE * count > len(ovl):
        raise ValueError('picture condition table extends outside OVL')
    return [decode_record(ovl, at + RECORD_SIZE * index, manifest, owner)
            for index in range(count)]


def plausible(record):
    # This version of Gateway has 734 known engine objects, including rooms.
    # The picture number is *not* an object ID and is not limited to 734.
    # Zero object ID intentionally means an unconditional picture layer.
    return (record['object_id'] <= 734 and
            record['relation_id'] <= 734 and
            record['extra_id'] <= 734 and
            record['picture_id'] != 0 and
            record['property'] <= 0xff)


def scan_calls(ovl, manifest, exe=None):
    """Find immediate OVL tables and resident DS records used by 425b."""
    calls = 0
    overlay_forms = 0
    resident_forms = 0
    overlay = []
    resident = []
    for section in manifest['sections']:
        begin = section['image_file_offset']
        end = min(begin + section['file_bytes'], len(ovl))
        for at in range(begin + 12, end - 4):
            if ovl[at] != 0x9a or struct.unpack_from('<HH', ovl, at + 1) != \
               CALL_TARGET:
                continue
            calls += 1
            pre = ovl[at - 12:at]
            # mov ax,count; push ax; mov ax,offset; mov dx,segment;
            # push dx; push ax; call far RTLink manager
            if tuple(pre[i] for i in (0, 3, 4, 7, 10, 11)) == \
               (0xb8, 0x50, 0xb8, 0xba, 0x52, 0x50):
                overlay_forms += 1
                count = struct.unpack_from('<H', pre, 1)[0]
                offset = struct.unpack_from('<H', pre, 5)[0]
                segment = struct.unpack_from('<H', pre, 8)[0]
                target = linked_pointer_file_offset(
                    manifest, section, offset, segment)
                if target is None:
                    continue
                owner, image_offset = target
                try:
                    records = inspect(ovl, manifest, owner['id'], image_offset,
                                      count)
                except ValueError:
                    continue
                if any(not plausible(r) for r in records):
                    continue
                overlay.append({
                    'call_section': section['id'], 'call_file_offset': at,
                    'table_section': owner['id'],
                    'table_file_offset': records[0]['file_offset'],
                    'count': count, 'records': records,
                })
                continue
            # mov ax,1; push ax; mov ax,DSoffset; push ds; push ax
            pre = ovl[at - 9:at]
            if tuple(pre[i] for i in (0, 3, 4, 7, 8)) != \
               (0xb8, 0x50, 0xb8, 0x1e, 0x50) or \
               struct.unpack_from('<H', pre, 1)[0] != 1:
                continue
            resident_forms += 1
            if exe is None:
                continue
            ds_offset = struct.unpack_from('<H', pre, 5)[0]
            file_offset = EXE_DATA_BASE + ds_offset
            if file_offset + RECORD_SIZE > len(exe):
                continue
            record = decode_record(exe, file_offset)
            if not plausible(record):
                continue
            resident.append({
                'call_section': section['id'], 'call_file_offset': at,
                'ds_offset': ds_offset, 'table_file_offset': file_offset,
                'count': 1, 'records': [record],
            })
    return calls, overlay_forms, resident_forms, overlay, resident


def live_cache(snapshot_prefix, count):
    """Inspect original DS:adf8 per-layer state in a retained RAM checkpoint.

    This cache is reused by scene updates. Interpret it only after checking
    that the checkpoint is in the room and at the correct compositor event.
    """
    ram, ds = snapshot_memory(snapshot_prefix)
    at = ds * 16 + 0xadf8
    if at + count > len(ram):
        raise ValueError('scene-layer cache outside RAM')
    return ds, list(ram[at:at + count])


def describe(record):
    callback = ('none' if (record['callback_offset'] == 0 and
                           record['callback_segment'] == 0) else
                f'{record["callback_segment"]:04x}:'
                f'{record["callback_offset"]:04x}')
    if record['callback_file'] is not None:
        sec, at = record['callback_file']
        callback += f' (ovl:0x{at:06x} section {sec})'
    return (f'object=0x{record["object_id"]:03x} '
            f'relation=0x{record["relation_id"]:03x} '
            f'picture=0x{record["picture_id"]:04x} '
            f'property=0x{record["property"]:02x} '
            f'extra=0x{record["extra_id"]:03x} callback={callback}')


def report(ovl, manifest, section_id, image_offset, count, snapshot=None):
    records = inspect(ovl, manifest, section_id, image_offset, count)
    lines = [f'Original 14-byte scene-layer condition records '
             f'(section {section_id}, ovl:0x{records[0]["file_offset"]:06x}):']
    for i, record in enumerate(records):
        lines.append(f'[{i}] ovl:0x{record["file_offset"]:06x} '
                     f'{describe(record)}')
    if snapshot is not None:
        ds, cache = live_cache(snapshot, count)
        lines.append(f'Original guest snapshot: DS={ds:04x}; '
                     f'cached layer states={cache}')
    lines.append('Layer state is distinct from native RGN picker eligibility.')
    return '\n'.join(lines)


def scan_report(ovl, manifest, details=False, exe=None):
    calls, overlay_forms, ds_forms, overlay, resident = \
        scan_calls(ovl, manifest, exe)
    unique_overlay = {(r['table_section'], r['table_file_offset']): r
                      for r in overlay}
    unique_ds = {r['ds_offset']: r for r in resident}
    overlay_records = sum(r['count'] for r in unique_overlay.values())
    lines = [f'Original conditional scene layers: {calls} direct calls to '
             f'1c24:425b, {overlay_forms} overlay-immediate forms '
             f'({len(overlay)} validated references, '
             f'{len(unique_overlay)} unique tables, '
             f'{overlay_records} records).',
             f'{ds_forms} resident-DS forms '
             f'({len(resident)} validated references, '
             f'{len(unique_ds)} unique records; '
             f'{"--exe required" if exe is None else "EXE decoded"}).']
    if exe is not None:
        lines.append(f'{len(overlay) + len(resident)} of {calls} calls '
                     f'validated; {overlay_records + len(unique_ds)} '
                     f'unique potential picture-layer records.')
    if details:
        for item in overlay:
            lines.append(f'call ovl:0x{item["call_file_offset"]:06x} '
                         f'(section {item["call_section"]}) -> '
                         f'ovl:0x{item["table_file_offset"]:06x} '
                         f'(section {item["table_section"]}), '
                         f'{item["count"]} records')
        for item in resident:
            lines.append(f'call ovl:0x{item["call_file_offset"]:06x} '
                         f'(section {item["call_section"]}) -> '
                         f'DS:0x{item["ds_offset"]:04x} '
                         f'(exe:0x{item["table_file_offset"]:06x}), '
                         f'{describe(item["records"][0])}')
    lines.append('These are candidate paint conditions, not evidence that '
                 'a layer was displayed in a particular snapshot.')
    return '\n'.join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('ovl', type=Path)
    parser.add_argument('--manifest', required=True,
                        help='gateway-overlay JSON manifest or - for stdin')
    parser.add_argument('--section', type=int)
    parser.add_argument('--image-offset', type=lambda s: int(s, 0))
    parser.add_argument('--count', type=int)
    parser.add_argument('--scan-calls', action='store_true')
    parser.add_argument('--exe', type=Path, help='pinned original GATEMAIN.EXE')
    parser.add_argument('--details', action='store_true')
    parser.add_argument('--snapshot-prefix', type=Path)
    args = parser.parse_args(argv)
    try:
        manifest = (json.load(sys.stdin) if args.manifest == '-' else
                    json.loads(Path(args.manifest).read_text()))
        ovl = args.ovl.read_bytes()
        if args.scan_calls:
            if args.snapshot_prefix is not None:
                parser.error('--snapshot-prefix requires a single table')
            exe = args.exe.read_bytes() if args.exe is not None else None
            if exe is not None and hashlib.sha256(exe).hexdigest() != EXE_SHA256:
                raise ValueError('EXE digest does not match pinned Gateway '
                                 'version; refusing resident DS mapping')
            print(scan_report(ovl, manifest, args.details, exe))
        else:
            if args.exe is not None:
                parser.error('--exe requires --scan-calls')
            if any(v is None for v in (args.section, args.image_offset,
                                       args.count)):
                parser.error('--section, --image-offset and --count required')
            print(report(ovl, manifest, args.section, args.image_offset,
                         args.count, args.snapshot_prefix))
    except (OSError, ValueError, KeyError, struct.error) as exc:
        parser.error(str(exc))


if __name__ == '__main__':
    main()
