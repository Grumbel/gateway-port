#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Summarize Gateway DOS RGN transfers and historical consumer samples.

Requires a freshly built gateway-probe with --trace-rgn-dos and
--trace-research. The safe, current trace records DOS transfers only. Older
memory-read hooks proved unreliable under Unicorn 2.1.4 and mistook reused
stack bytes for live RGN data. Neither format proves a mouse target active.
"""
import argparse
from pathlib import Path


def describe_gateway_rgn_offset(data, offset):
    """Decode a sampled offset as a Gateway 1 header/rectangle candidate.

    This is a static description, not a claim that the rectangle was visible
    or that the consuming instruction performed a hit test.
    """
    if len(data) < 6 or offset < 0 or offset >= len(data):
        return None
    first_rectangle = int.from_bytes(data[:2], 'little')
    if not first_rectangle or first_rectangle % 6 or first_rectangle > len(data):
        return None
    if offset < first_rectangle:
        return f'header[{offset // 6}] word+{offset % 6}'
    base = first_rectangle + ((offset - first_rectangle) // 6) * 6
    if base + 6 > len(data):
        return None
    object_id = int.from_bytes(data[base:base + 2], 'little')
    top_left = int.from_bytes(data[base + 2:base + 4], 'little')
    bottom_right = int.from_bytes(data[base + 4:base + 6], 'little')
    x0, y0 = 2 * (top_left & 0xff), top_left >> 8
    x1, y1 = 2 * (bottom_right & 0xff), bottom_right >> 8
    target = f'0x{object_id:03x}' if object_id else 'header-default'
    return (f'rect[{(base - first_rectangle) // 6}] object={target} '
            f'room-xy=({x0},{y0})-({x1},{y1})')


def parse_fields(line):
    return dict(part.split('=', 1) for part in line.split()[1:] if '=' in part)


def parse_trace(lines):
    summary = None
    consumers = {}
    samples = {}
    dos_reads = []
    for line in lines:
        if line.startswith('rgn-summary '):
            if summary is not None:
                raise ValueError('duplicate RGN summary')
            fields = parse_fields(line)
            try:
                summary = {key: int(fields[key]) for key in
                           ('buffers', 'evictions', 'active', 'consumers', 'dropped')}
                summary['mode'] = fields.get('mode', 'legacy')
            except (KeyError, ValueError) as error:
                raise ValueError(f'invalid RGN summary: {line.strip()}') from error
        elif line.startswith('rgn-consumer '):
            fields = parse_fields(line)
            try:
                key = (fields['file'], int(fields['cs'], 16), int(fields['ip'], 16))
                if key in consumers:
                    raise ValueError('duplicate RGN consumer')
                consumers[key] = dict(offset=int(fields['first_offset'], 16),
                                      hits=int(fields['hits']),
                                      first_ms=int(fields['first_ms']),
                                      last_ms=int(fields['last_ms']))
                if consumers[key]['hits'] < 1 or \
                        consumers[key]['last_ms'] < consumers[key]['first_ms']:
                    raise ValueError('invalid hit count or time ordering')
            except (KeyError, ValueError) as error:
                raise ValueError(f'invalid RGN consumer: {line.strip()}') from error
        elif line.startswith('rgn-guest-read '):
            fields = parse_fields(line)
            try:
                key = (fields['file'], int(fields['cs'], 16), int(fields['ip'], 16))
                sample = (int(fields['ms']), int(fields['offset'], 16),
                          int(fields['address'], 16), int(fields['size'], 16))
                samples.setdefault(key, []).append(sample)
            except (KeyError, ValueError) as error:
                raise ValueError(f'invalid RGN guest read: {line.strip()}') from error
        elif line.startswith('rgn-buffer '):
            fields = parse_fields(line)
            if 'cs' in fields and 'ip' in fields:
                try:
                    dos_reads.append((fields['file'], int(fields['cs'], 16),
                                      int(fields['ip'], 16), int(fields['ms']),
                                      int(fields['offset'], 16), int(fields['size'], 16),
                                      int(fields['address'], 16), int(fields['ss'], 16),
                                      int(fields['sp'], 16)))
                except (KeyError, ValueError) as error:
                    raise ValueError(f'invalid RGN DOS read: {line.strip()}') from error
    if summary is None:
        raise ValueError('missing RGN census; run gateway-probe with --trace-rgn-dos')
    if len(consumers) != summary['consumers']:
        raise ValueError('RGN consumer count disagrees with summary')
    for key, values in samples.items():
        if key not in consumers or len(values) > consumers[key]['hits']:
            raise ValueError('RGN sample without corresponding consumer count')
    if summary['mode'] == 'dos-only' and len(dos_reads) != summary['buffers']:
        raise ValueError('RGN DOS read count disagrees with summary')
    return summary, consumers, samples, dos_reads


def summarize(lines, focus=None, limit=40, minimum_hits=1, rgn_data=None):
    summary, consumers, samples, dos_reads = parse_trace(lines)
    if summary['mode'] == 'dos-only':
        grouped = {}
        for file, cs, ip, ms, offset, size, address, ss, sp in dos_reads:
            if focus is None or file.upper() == focus.upper():
                grouped.setdefault((file, cs, ip), []).append(
                    (ms, offset, size, address, ss, sp))
        rows = [(key, reads) for key, reads in grouped.items()
                if len(reads) >= minimum_hits]
        rows.sort(key=lambda item: (-len(item[1]), item[0]))
        result = [f'RGN DOS reads: {summary["buffers"]}; no guest memory-read '
                  'hooks installed (safe mode).',
                  'file              CS:IP      reads    ms(first-last)']
        for (file, cs, ip), reads in rows[:limit]:
            result.append(f'{file[:16]:16}  {cs:04x}:{ip:04x}  {len(reads):>5}    '
                          f'{reads[0][0]}-{reads[-1][0]}')
            data = (rgn_data or {}).get(file)
            for ms, offset, size, address, ss, sp in reads[:3]:
                entry = (f'    DOS ms={ms} file+0x{offset:x} size={size} '
                         f'guest=0x{address:x} SS:SP={ss:04x}:{sp:04x}')
                if data is not None:
                    description = describe_gateway_rgn_offset(data, offset)
                    if description:
                        entry += '  ' + description
                result.append(entry)
        if len(rows) > limit:
            result.append(f'... {len(rows) - limit} more DOS call sites (use --limit).')
        if not rows:
            result.append('No matching RGN DOS calls.')
        result.append('The destination is often a temporary stack buffer; '
                      'no claim about later consumers or visible hitboxes is implied.')
        return '\n'.join(result)
    rows = [(key, info) for key, info in consumers.items()
            if (focus is None or key[0].upper() == focus.upper()) and
            info['hits'] >= minimum_hits]
    rows.sort(key=lambda row: (-row[1]['hits'], row[0]))
    result = ['WARNING: legacy Unicorn memory hooks changed guest execution and '
              'mistook reused stack data for RGN consumers.',
              f"RGN buffers: {summary['buffers']} loaded, "
              f"{summary['active']} active, {summary['evictions']} replaced/evicted; "
              f"{len(consumers)} distinct guest consumer sites."]
    if summary['dropped']:
        result.append(f"WARNING: {summary['dropped']} guest reads were dropped after the "
                      'consumer census reached its capacity; ranking is incomplete.')
    result.append('file              CS:IP      hits    ms(first-last)    first RGN offset')
    result.append('----------------  ---------  ------  ----------------  ----------------')
    for key, info in rows[:limit]:
        file, cs, ip = key
        result.append(f'{file[:16]:16}  {cs:04x}:{ip:04x}  {info["hits"]:>6}  '
                      f'{info["first_ms"]:>6}-{info["last_ms"]:<6}    '
                      f'0x{info["offset"]:04x}')
        data = (rgn_data or {}).get(file)
        for ms, offset, address, size in samples.get(key, []):
            sample = f'    sample ms={ms} file+0x{offset:x} guest=0x{address:x} bytes={size}'
            if data is not None:
                description = describe_gateway_rgn_offset(data, offset)
                if description is not None:
                    sample += f'  {description}'
            result.append(sample)
    if not rows:
        result.append('No tracked RGN buffer reads in this time window. The engine may '
                      'have copied the data, or no RGN file was loaded.')
    if len(rows) > limit:
        result.append(f'... {len(rows) - limit} more consumer sites (use --limit).')
    result.append('Consumer sites are *candidates* for further disassembly; '
                  'none proves hitbox visibility or mouse selection.')
    return '\n'.join(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path, help='research trace with RGN memory census')
    parser.add_argument('--file', help='filter to one DOS RGN filename')
    parser.add_argument('--limit', type=int, default=40)
    parser.add_argument('--min-hits', type=int, default=1)
    parser.add_argument('--rgn-dir', type=Path, help='decode RGN offsets against original '
                        'read-only Gateway 1 data files')
    args = parser.parse_args()
    if args.limit < 1 or args.min_hits < 1:
        parser.error('--limit and --min-hits must be positive')
    try:
        rgn_data = None
        if args.rgn_dir is not None:
            rgn_data = {path.name.upper(): path.read_bytes()
                        for path in args.rgn_dir.iterdir()
                        if path.is_file() and path.suffix.upper() == '.RGN'}
        print(summarize(args.trace.read_text().splitlines(), args.file,
                        args.limit, args.min_hits, rgn_data))
    except (OSError, ValueError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
