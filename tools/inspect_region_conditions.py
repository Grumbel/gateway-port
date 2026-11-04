# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Inspect the original ten-byte conditional region records.

The section-0 routine at GATEMAIN.OVL:0x2680 (section 0) consumes these
records. This is a read-only inspection tool, not a reconstructed visibility
oracle: callbacks are native x86 and the resulting picker mask can be only
one of several gates between an object and its rendered appearance.
"""

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path


RECORD_SIZE = 10
# This EXE's resident DGROUP layout has been independently compared with
# original-game RAM checkpoints. Do not interpret unrelated EXE builds using
# its static DS-to-file mapping.
EXE_SHA256 = '1785bf60f196836c0542dafa4a00085f6b1f3837992f2cc14da12503bc02abcb'
EXE_DATA_BASE = 0x1ba93


def section_by_id(manifest, section_id):
    matches = [s for s in manifest['sections'] if s['id'] == section_id]
    if len(matches) != 1:
        raise ValueError(f'section {section_id} absent or duplicated in manifest')
    return matches[0]


def callback_file_offset(manifest, owner, offset, segment):
    """Resolve a linked far pointer, preferring the owning RTLink section."""
    if offset == segment == 0:
        return None
    candidates = []
    for s in manifest['sections']:
        delta = (segment - s['linked_segment']) * 16 + offset
        if 0 <= delta < s['file_bytes']:
            candidates.append((s['id'], s['image_file_offset'] + delta))
    owned = [c for c in candidates if c[0] == owner['id']]
    if len(owned) == 1:
        return owned[0]
    if len(candidates) == 1:
        return candidates[0]
    return None


def linked_pointer_file_offset(manifest, owner, offset, segment):
    """Convert a linked far data pointer to a section-relative file address."""
    candidates = []
    for s in manifest['sections']:
        delta = (segment - s['linked_segment']) * 16 + offset
        if 0 <= delta < s['file_bytes']:
            candidates.append((s, delta))
    owned = [c for c in candidates if c[0]['id'] == owner['id']]
    if len(owned) == 1:
        return owned[0]
    if len(candidates) == 1:
        return candidates[0]
    return None


def inspect(ovl, manifest, section_id, image_offset, count):
    owner = section_by_id(manifest, section_id)
    size = owner['file_bytes']
    if count < 1 or count > 4096 or image_offset < 0 or \
       image_offset + count * RECORD_SIZE > size:
        raise ValueError('condition table extends outside initialized section')
    source = owner['image_file_offset'] + image_offset
    if source + count * RECORD_SIZE > len(ovl):
        raise ValueError('condition table extends past original OVL file')
    records = []
    for index in range(count):
        at = source + index * RECORD_SIZE
        object_id, relation_id, slot, callback_ip, callback_cs = \
            struct.unpack_from('<5H', ovl, at)
        records.append({
            'file_offset': at,
            'object_id': object_id,
            'relation_id': relation_id,
            'slot': slot,
            'callback_segment': callback_cs,
            'callback_offset': callback_ip,
            'callback_file': callback_file_offset(
                manifest, owner, callback_ip, callback_cs),
        })
    return records


def scan_constant_tables(ovl, manifest):
    """Find overlay-backed calls through the original 1c24:4251 RTLink gate.

    A second compiler form supplies a resident DS pointer and is handled
    separately by scan_resident_ds_calls.
    """
    calls = 0
    immediate = 0
    tables = []
    for section in manifest['sections']:
        begin = section['image_file_offset']
        end = min(begin + section['file_bytes'], len(ovl))
        for at in range(begin + 12, end - 4):
            if ovl[at] != 0x9a or struct.unpack_from('<HH', ovl, at + 1) != \
               (0x4251, 0x1c24):
                continue
            calls += 1
            # mov ax,count; push ax; mov ax,offset; mov dx,segment;
            # push dx; push ax; call far manager:4251
            pre = ovl[at - 12:at]
            if tuple(pre[i] for i in (0, 3, 4, 7, 10, 11)) != \
               (0xb8, 0x50, 0xb8, 0xba, 0x52, 0x50):
                continue
            immediate += 1
            count = struct.unpack_from('<H', pre, 1)[0]
            offset = struct.unpack_from('<H', pre, 5)[0]
            segment = struct.unpack_from('<H', pre, 8)[0]
            target = linked_pointer_file_offset(manifest, section, offset, segment)
            if target is None:
                continue
            owner, image_offset = target
            try:
                records = inspect(ovl, manifest, owner['id'], image_offset, count)
            except ValueError:
                continue
            # Gateway 1 has 734 native objects and at most 35 group slots.
            # Reject tables whose decoded fields could just be instruction
            # bytes accidentally resembling the immediate-call pattern.
            if any(not 0 < r['object_id'] <= 734 or r['relation_id'] > 734 or
                   r['slot'] >= 35 or
                   ((r['callback_segment'] != 0 or r['callback_offset'] != 0) and
                    r['callback_file'] is None) for r in records):
                continue
            tables.append({
                'call_section': section['id'],
                'call_file_offset': at,
                'table_section': owner['id'],
                'table_file_offset': records[0]['file_offset'],
                'count': count,
                'callback_count': sum(r['callback_segment'] != 0 or
                                      r['callback_offset'] != 0 for r in records),
                'relation_count': sum(r['relation_id'] != 0 for r in records),
            })
    return calls, immediate, tables


def scan_resident_ds_calls(ovl, manifest, exe, exe_data_base=EXE_DATA_BASE):
    """Resolve the second compiler form: a one-record table in EXE DGROUP.

    The linked call's argument is pushed from DS, not an OVL-linked segment.
    Its table's offset is a *resident DS offset*, rather than an OVL offset.
    The caller must verify the pinned EXE hash when using a real game EXE.
    """
    forms = 0
    tables = []
    for section in manifest['sections']:
        begin = section['image_file_offset']
        end = min(begin + section['file_bytes'], len(ovl))
        for at in range(begin + 9, end - 4):
            if ovl[at] != 0x9a or struct.unpack_from('<HH', ovl, at + 1) != \
               (0x4251, 0x1c24):
                continue
            # mov ax,1; push ax; mov ax,ds_offset; push ds; push ax
            pre = ovl[at - 9:at]
            if tuple(pre[i] for i in (0, 3, 4, 7, 8)) != \
               (0xb8, 0x50, 0xb8, 0x1e, 0x50) or \
               struct.unpack_from('<H', pre, 1)[0] != 1:
                continue
            forms += 1
            ds_offset = struct.unpack_from('<H', pre, 5)[0]
            file_offset = exe_data_base + ds_offset
            if file_offset + RECORD_SIZE > len(exe):
                continue
            object_id, relation_id, slot, callback_ip, callback_cs = \
                struct.unpack_from('<5H', exe, file_offset)
            if not 0 < object_id <= 734 or relation_id > 734 or slot >= 35:
                continue
            # Resident callback targets (when present) may be outside OVL.
            # The original game contains a pointer to the resident manager
            # callback at 1c24:4495; do not demand an overlay mapping.
            if (callback_ip or callback_cs) and (callback_ip, callback_cs) != \
               (0x4495, 0x1c24):
                continue
            tables.append({
                'call_section': section['id'],
                'call_file_offset': at,
                'table_section': None,
                'table_file_offset': file_offset,
                'ds_offset': ds_offset,
                'count': 1,
                'callback_count': int(bool(callback_ip or callback_cs)),
                'relation_count': int(bool(relation_id)),
                'object_id': object_id,
                'relation_id': relation_id,
                'callback_segment': callback_cs,
                'callback_offset': callback_ip,
            })
    return forms, tables


def scan_report(ovl, manifest, details=False, exe=None):
    calls, immediate, tables = scan_constant_tables(ovl, manifest)
    unique = {(r['table_section'], r['table_file_offset']): r for r in tables}
    entries = sum(r['count'] for r in unique.values())
    callbacks = sum(r['callback_count'] for r in unique.values())
    relations = sum(r['relation_count'] for r in unique.values())
    sections = len({r['table_section'] for r in unique.values()})
    lines = ['Original Gateway conditional RGN rules: '
             f'{calls} direct evaluator calls, {immediate} overlay-immediate forms, '
             f'{len(tables)} validated overlay references, '
             f'{len(unique)} unique overlay tables.',
             f'{entries} plausible overlay rule records across {sections} sections; '
             f'{callbacks} callback references, {relations} relation requirements.']
    resident = []
    if exe is not None:
        forms, resident = scan_resident_ds_calls(ovl, manifest, exe)
        unique_ds = {r['ds_offset']: r for r in resident}
        ds_count = len(unique_ds)
        lines.append(
            f'{forms} DS-pointer forms, {len(resident)} validated EXE references, '
            f'{ds_count} unique resident records; '
            f'{sum(r["callback_count"] for r in unique_ds.values())} callbacks, '
            f'{sum(r["relation_count"] for r in unique_ds.values())} relations.')
        lines.append(
            f'{len(tables) + len(resident)} of {calls} direct calls validated; '
            f'{entries + ds_count} unique rule records across overlay and EXE.')
    else:
        lines.append('Resident-DS forms require --exe to resolve.')
    lines.append('Eligibility for mouse selection does not prove image visibility.')
    if details:
        for entry in tables:
            lines.append(
                f'call ovl:0x{entry["call_file_offset"]:06x} '
                f'(section {entry["call_section"]}) -> '
                f'table ovl:0x{entry["table_file_offset"]:06x} '
                f'(section {entry["table_section"]}): '
                f'{entry["count"]} rules, {entry["callback_count"]} callbacks, '
                f'{entry["relation_count"]} relations')
        for entry in resident:
            callback = (f'{entry["callback_segment"]:04x}:'
                        f'{entry["callback_offset"]:04x}'
                        if entry['callback_count'] else 'none')
            lines.append(
                f'call ovl:0x{entry["call_file_offset"]:06x} '
                f'(section {entry["call_section"]}) -> '
                f'resident DS:0x{entry["ds_offset"]:04x} '
                f'(exe:0x{entry["table_file_offset"]:06x}): '
                f'object 0x{entry["object_id"]:03x}, '
                f'relation 0x{entry["relation_id"]:03x}, callback {callback}')
    return '\n'.join(lines)


def live_picker(memory, ds, records):
    """Read the guest's current group/index/skip table without executing code."""
    base = ds * 16

    def byte(offset):
        at = base + offset
        if not 0 <= at < len(memory):
            raise ValueError('native picker address outside guest RAM')
        return memory[at]

    def word(offset):
        return byte(offset) | (byte(offset + 1) << 8)

    group = word(0xa6ce)
    if group >= 6:
        raise ValueError('unsupported active native picker group')
    count = byte(0xa8d4 + group)
    if count > 35:
        raise ValueError('native picker group exceeds 35 slots')
    results = []
    for record in records:
        slots = ([record['slot']] if record['slot'] else
                 [i for i in range(count)
                  if word(0xa8da + 2 * i) == record['object_id']])
        resolved = []
        for slot in slots:
            if slot >= count:
                continue
            index = byte(0xa974 + 35 * group + slot)
            if index >= 64:
                continue
            resolved.append((slot, index, byte(0xa920 + index)))
        results.append(resolved)
    return group, results


def snapshot_memory(prefix):
    """Read existing private RAM checkpoint and its independently captured DS."""
    prefix = str(prefix)
    data = Path(prefix + '.bin').read_bytes()
    lines = Path(prefix + '.debug.txt').read_text().splitlines()
    candidates = [line.split('=', 1)[1] for line in lines
                  if line.startswith('object_ds=')]
    if len(candidates) != 1:
        raise ValueError('snapshot has no unique object_ds metadata')
    ds = int(candidates[0], 16)
    if not 0 < ds <= 0xffff:
        raise ValueError('invalid object_ds metadata')
    return data, ds


def report(ovl, manifest, section_id, image_offset, count, snapshot=None):
    records = inspect(ovl, manifest, section_id, image_offset, count)
    lines = [f'GATEMAIN.OVL section {section_id}, ten-byte condition records '
             f'at file offset 0x{records[0]["file_offset"]:06x} ({count} entries)',
             'This routine disables matching rectangles on failed relation '
             'or a nonzero callback; no claim of rendered visibility.']
    for i, r in enumerate(records):
        selector = (f'slot {r["slot"]}' if r['slot']
                    else f'object 0x{r["object_id"]:03x}')
        relation = (f'object 0x{r["object_id"]:03x} has immediate relation '
                    f'to 0x{r["relation_id"]:03x}' if r['relation_id']
                    else 'none')
        if r['callback_segment'] == r['callback_offset'] == 0:
            callback = 'none'
        else:
            callback = (f'{r["callback_segment"]:04x}:'
                        f'{r["callback_offset"]:04x}')
            if r['callback_file'] is not None:
                section, at = r['callback_file']
                callback += f' (ovl:0x{at:06x}, section {section})'
            else:
                callback += ' (not uniquely mapped)'
        lines.append(f'[{i}] ovl:0x{r["file_offset"]:06x} '
                     f'object=0x{r["object_id"]:03x} selector={selector}; '
                     f'relation={relation}; callback={callback}')
    if snapshot is not None:
        memory, ds = snapshot_memory(snapshot)
        group, states = live_picker(memory, ds, records)
        lines.append(f'Original guest snapshot: DS={ds:04x}, active group={group}')
        for i, matched in enumerate(states):
            detail = (', '.join(f'slot {slot} -> index {index} '
                                f'flag 0x{flags:02x} '
                                f'({"skipped" if flags & 0x80 else "not skipped"})'
                                for slot, index, flags in matched)
                      if matched else 'no corresponding live picker slot')
            lines.append(f'[{i}] {detail}')
    return '\n'.join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('ovl', type=Path, help='original GATEMAIN.OVL')
    parser.add_argument('--manifest', type=str, required=True,
                        help='gateway-overlay JSON, or - for standard input')
    parser.add_argument('--section', type=int)
    parser.add_argument('--image-offset', type=lambda s: int(s, 0),
                        help='table offset within the RTLink section image')
    parser.add_argument('--count', type=int)
    parser.add_argument('--scan-calls', action='store_true',
                        help='discover calls to the conditional-region evaluator')
    parser.add_argument('--exe', type=Path,
                        help='pinned original GATEMAIN.EXE to resolve resident DS tables')
    parser.add_argument('--details', action='store_true',
                        help='list every discovered condition-table call')
    parser.add_argument('--snapshot-prefix', type=Path,
                        help='optional original .bin/.debug.txt RAM checkpoint')
    args = parser.parse_args(argv)
    try:
        if args.manifest == '-':
            manifest = json.load(sys.stdin)
        else:
            manifest = json.loads(Path(args.manifest).read_text())
        ovl = args.ovl.read_bytes()
        if args.scan_calls:
            if args.snapshot_prefix is not None:
                parser.error('--snapshot-prefix requires a single specified table')
            exe = None
            if args.exe is not None:
                exe = args.exe.read_bytes()
                if hashlib.sha256(exe).hexdigest() != EXE_SHA256:
                    raise ValueError('EXE digest does not match the pinned '
                                     'Gateway version; refusing DS offset mapping')
            print(scan_report(ovl, manifest, args.details, exe))
        else:
            if args.exe is not None:
                parser.error('--exe is only valid with --scan-calls')
            if any(value is None for value in
                   (args.section, args.image_offset, args.count)):
                parser.error('--section, --image-offset, and --count are required')
            print(report(ovl, manifest, args.section,
                         args.image_offset, args.count, args.snapshot_prefix))
    except (OSError, ValueError, KeyError, struct.error) as exc:
        parser.error(str(exc))


if __name__ == '__main__':
    main()
