#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Parse and validate sampled, section-qualified native RTLink call/return traces.

Only the original probe can record these observations; this parser never
executes native code or infers a successful story action from a return value.
"""
import argparse
from collections import Counter
import json
from pathlib import Path


def fields(line, prefix):
    if not line.startswith(prefix + ' '):
        raise ValueError(f'expected {prefix} row')
    result = {}
    for token in line[len(prefix) + 1:].split():
        if '=' not in token:
            raise ValueError('invalid RTLink event token')
        name, value = token.split('=', 1)
        if not name or not value or name in result:
            raise ValueError('duplicate or empty RTLink event field')
        result[name] = value
    return result


def decimal(value):
    if not value or not value.isdecimal():
        raise ValueError('invalid RTLink event decimal')
    return int(value)


def hexword(value):
    if not value or any(char not in '0123456789abcdef' for char in value):
        raise ValueError('invalid RTLink event hexadecimal')
    number = int(value, 16)
    if number > 0xffff:
        raise ValueError('RTLink event word out of range')
    return number


def far_pointer(value):
    parts = value.split(':')
    if len(parts) != 2:
        raise ValueError('RTLink event far pointer invalid')
    return hexword(parts[0]), hexword(parts[1])


def parse_events(lines):
    """Return complete and unfinished events; reject mispaired or corrupt rows."""
    entries = {}
    changed = Counter()
    summary = None
    last_id = 0
    for raw in lines:
        line = raw.strip()
        if not line:
            continue
        if summary is not None:
            raise ValueError('rows after RTLink event footer')
        prefix = line.split(' ', 1)[0]
        row = fields(line, prefix)
        if prefix == 'rtlink-enter':
            required = {'id', 'ms', 'section', 'target', 'caller', 'ss', 'sp',
                        'ax', 'bx', 'cx', 'dx', 'args', 'object_state'}
            if set(row) != required:
                raise ValueError('RTLink entry missing required fields')
            identifier = decimal(row['id'])
            section = decimal(row['section'])
            if identifier != last_id + 1 or section >= 58:
                raise ValueError('invalid RTLink event ID or section')
            last_id = identifier
            target = far_pointer(row['target'])
            far_pointer(row['caller'])
            for name in ('ss', 'sp', 'ax', 'bx', 'cx', 'dx'):
                hexword(row[name])
            args = row['args'].split(',')
            if len(args) != 4:
                raise ValueError('RTLink event requires four stack words')
            for value in args:
                hexword(value)
            if row['object_state'] not in ('verified', 'unavailable'):
                raise ValueError('unknown RTLink object snapshot status')
            entries[identifier] = dict(id=identifier, section=section, target=target,
                                       start_ms=decimal(row['ms']), args=tuple(
                                           hexword(value) for value in args),
                                       ax_in=hexword(row['ax']),
                                       ss=hexword(row['ss']), sp=hexword(row['sp']),
                                       object_before=row['object_state'],
                                       ax_out=None, returned=False, changes=[])
        elif prefix == 'rtlink-change':
            if set(row) != {'id', 'object', 'byte', 'before', 'after'}:
                raise ValueError('RTLink change missing required fields')
            identifier = decimal(row['id'])
            event = entries.get(identifier)
            if event is None or event['returned'] or event['object_before'] != 'verified':
                raise ValueError('RTLink change outside active verified event')
            obj = hexword(row['object'])
            byte = hexword(row['byte'])
            before, after = hexword(row['before']), hexword(row['after'])
            if not 1 <= obj <= 734 or byte > 127 or before > 255 or after > 255 or before == after:
                raise ValueError('invalid native object-record delta')
            if any(item['object'] == obj and item['byte'] == byte for item in event['changes']):
                raise ValueError('duplicate native object-record delta')
            event['changes'].append(dict(object=obj, byte=byte, before=before, after=after))
            changed[identifier] += 1
        elif prefix == 'rtlink-return':
            if set(row) != {'id', 'ms', 'ax', 'bx', 'cx', 'dx', 'ss', 'sp',
                            'object_bytes_changed', 'object_state'}:
                raise ValueError('RTLink return missing required fields')
            identifier = decimal(row['id'])
            event = entries.get(identifier)
            if event is None or event['returned']:
                raise ValueError('RTLink return missing or duplicate entry')
            for name in ('ax', 'bx', 'cx', 'dx', 'ss', 'sp'):
                hexword(row[name])
            if hexword(row['ss']) != event['ss'] or \
                    (hexword(row['sp']) - event['sp']) % 65536 > 16:
                raise ValueError('RTLink return has mismatched native stack frame')
            event['end_ms'] = decimal(row['ms'])
            if event['end_ms'] < event['start_ms']:
                raise ValueError('negative RTLink event duration')
            if decimal(row['object_bytes_changed']) != changed[identifier] or \
                    row['object_state'] not in ('verified', 'unavailable') or \
                    (row['object_state'] == 'unavailable' and changed[identifier]):
                raise ValueError('RTLink record-change count or snapshot mismatch')
            event['returned'] = True
            event['ax_out'] = hexword(row['ax'])
            event['object_after'] = row['object_state']
        elif prefix == 'rtlink-events':
            if set(row) != {'selected', 'returned', 'unfinished', 'limit'}:
                raise ValueError('invalid RTLink event footer')
            summary = {key: decimal(value) for key, value in row.items()}
            if summary['selected'] != len(entries) or \
                    summary['returned'] != sum(e['returned'] for e in entries.values()) or \
                    summary['unfinished'] != sum(not e['returned'] for e in entries.values()) or \
                    summary['selected'] > summary['limit'] or not summary['limit']:
                raise ValueError('RTLink event footer disagrees with entries')
        else:
            raise ValueError('unknown RTLink event row')
    if summary is None:
        raise ValueError('missing RTLink event footer')
    return list(entries.values()), summary


def summarize_events(entries):
    """Group sampled returns without mistaking the sample for a full census."""
    groups = {}
    for event in entries:
        key = (event['section'], *event['target'])
        if key not in groups:
            groups[key] = dict(calls=0, returns=Counter(), changes=0,
                               first_ms=event['start_ms'], last_ms=event['start_ms'])
        group = groups[key]
        group['calls'] += 1
        group['returns'][event['ax_out'] if event['returned'] else None] += 1
        group['changes'] += len(event['changes'])
        group['first_ms'] = min(group['first_ms'], event['start_ms'])
        group['last_ms'] = max(group['last_ms'], event['start_ms'])
    return groups


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--format', choices=('text', 'summary', 'json'), default='text')
    args = parser.parse_args()
    try:
        entries, summary = parse_events(args.trace.read_text().splitlines())
    except (OSError, ValueError) as error:
        parser.exit(1, f'inspect_rtlink_events: {error}\n')
    if args.format == 'json':
        print(json.dumps(dict(schema_version=1, summary=summary, events=entries), indent=2))
    else:
        print('Sampled %d native RTLink calls, %d matched returns, %d unfinished' % (
            summary['selected'], summary['returned'], summary['unfinished']))
        if args.format == 'summary':
            for (section, cs, ip), group in sorted(summarize_events(entries).items()):
                values = ', '.join(
                    ('unfinished' if ax is None else 'AX=%04x' % ax) + ':%d' % count
                    for ax, count in sorted(group['returns'].items(),
                                            key=lambda item: (item[0] is None, item[0] or 0)))
                print('section=%d target=%04x:%04x calls=%d returns=%s '
                      'object_bytes_changed=%d first_ms=%d last_ms=%d' % (
                          section, cs, ip, group['calls'], values, group['changes'],
                          group['first_ms'], group['last_ms']))
        else:
            for event in entries:
                print('section=%d target=%04x:%04x AX=%04x -> %s; changed bytes=%d' % (
                    event['section'], *event['target'], event['ax_in'],
                    ('%04x' % event['ax_out']) if event['returned'] else 'unfinished',
                    len(event['changes'])))


if __name__ == '__main__':
    main()
