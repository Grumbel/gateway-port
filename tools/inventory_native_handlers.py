#!/usr/bin/env python3
"""Inventory native object handlers and room callbacks against RTLink calls.

The input is a previously exported object JSON snapshot and the fingerprinted
Ghidra import plan. Optional --rtlink-calls is an *invocation* census from the
real Unicorn RTLink trampoline manager, not an executed-basic-block coverage
trace. Multiple sections reuse physical addresses; the section word in each
verified trampoline is part of the identity of a native entry.
"""

import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re


CALL_RE = re.compile(r'^rtlink-call section=(\d+) target=([0-9a-f]+):([0-9a-f]+) '
                     r'count=(\d+) first_ms=(\d+) last_ms=(\d+)$')
CENSUS_RE = re.compile(r'^rtlink-census manager_hits=(\d+) validated=(\d+) '
                       r'unrecognized=(\d+)$')


def parse_calls(lines):
    """Read the opt-in probe census; counts are decimal, far pointers hex."""
    lines = [line.strip() for line in lines if line.strip()]
    if not lines or not (header := CENSUS_RE.fullmatch(lines[0])):
        raise ValueError('missing or invalid RTLink call census header')
    hits, validated, rejected = (int(value) for value in header.groups())
    if hits != validated + rejected:
        raise ValueError('inconsistent manager hit counts')
    calls = {}
    for line in lines[1:]:
        match = CALL_RE.fullmatch(line)
        if match is None:
            raise ValueError('invalid RTLink call row')
        sid, cs, ip, count, first, last = match.groups()
        key = int(sid), int(cs, 16), int(ip, 16)
        record = dict(calls=int(count), first_ms=int(first), last_ms=int(last))
        if key in calls or not 0 <= key[0] < 58 or not all(0 <= n <= 0xffff for n in key[1:]):
            raise ValueError('duplicate or invalid RTLink call target')
        if not 0 < record['calls'] <= validated or not 0 <= record['first_ms'] <= record['last_ms']:
            raise ValueError('invalid RTLink call count or timestamps')
        calls[key] = record
    if sum(item['calls'] for item in calls.values()) != validated:
        raise ValueError('RTLink call total does not match manager header')
    return calls, dict(manager_hits=hits, validated_calls=validated,
                       unrecognized_manager_hits=rejected, distinct_targets=len(calls))


def map_entry(reference, sections):
    """Resolve the original section+offset; never use linear address alone."""
    sid, cs, ip = reference
    section = sections.get(sid)
    if section is None:
        raise ValueError(f'RTLink section {sid} is not in the Ghidra plan')
    offset = (cs - section['runtime_segment']) * 16 + ip
    if not 0 <= offset < section['file_bytes']:
        raise ValueError(f'RTLink target {sid}:{cs:04x}:{ip:04x} is not file-backed')
    return dict(section=sid, image_offset=offset,
                file_offset=section['original_file_offset'] + offset,
                target_segment=cs, target_offset=ip)


def inventory(objects_report, plan, callbacks_report=None, calls=None, call_header=None):
    if plan.get('schema_version') != 1 or not isinstance(plan.get('sections'), list):
        raise ValueError('unsupported Ghidra import plan')
    sections = {section['id']: section for section in plan['sections']}
    if len(sections) != len(plan['sections']):
        raise ValueError('duplicate Ghidra section ID')
    objects = objects_report['objects']
    if objects_report.get('object_count') != len(objects):
        raise ValueError('object count mismatch')
    if len({obj['object'] for obj in objects}) != len(objects):
        raise ValueError('duplicate native object ID')
    handlers = {}
    for obj in objects:
        hid = obj['handler']
        if not hid:
            continue
        stub = obj.get('rtlink_stub')
        if stub is None:
            raise ValueError(f'object {obj["object"]}: missing verified native stub')
        key = (stub['section_word'], stub['target_segment'], stub['target_offset'])
        entry = map_entry(key, sections)
        if hid in handlers and handlers[hid]['entry'] != entry:
            raise ValueError(f'handler {hid} has conflicting native entry pointers')
        record = handlers.setdefault(hid, dict(handler=hid, entry=entry, objects=[]))
        record['objects'].append(dict(id=obj['object'], type=obj['type'],
                                      name=obj.get('name', '')))
    if objects_report.get('handler_count') != len(handlers):
        raise ValueError('unique handler count mismatch')
    entries = [tuple(item['entry'][field] for field in
                     ('section', 'target_segment', 'target_offset')) for item in handlers.values()]
    if len(entries) != len(set(entries)):
        raise ValueError('different handler IDs unexpectedly alias one native entry')

    callbacks = []
    if callbacks_report is not None:
        if callbacks_report.get('schema_version') != 1 or \
                callbacks_report.get('table_count') != 44:
            raise ValueError('unsupported native callback table')
        seen = set()
        for item in callbacks_report['callbacks']:
            cid = item['callback_id']
            if cid in seen:
                raise ValueError('duplicate room callback ID')
            seen.add(cid)
            if item.get('status') != 'rtlink':
                raise ValueError(f'callback {cid} has no validated RTLink trampoline')
            ref = item['rtlink_reference']
            key = ref['section'], ref['target_segment'], ref['target_offset']
            callbacks.append(dict(callback_id=cid, uses=len(item['uses']),
                                  entry=map_entry(key, sections)))

    call_keys = set(calls or {})
    known_keys = set(entries) | {
        tuple(item['entry'][field] for field in ('section', 'target_segment', 'target_offset'))
        for item in callbacks
    }
    section_stats = defaultdict(lambda: dict(object_handlers=0, room_callbacks=0,
                                             observed_object_handlers=0,
                                             observed_room_callbacks=0))
    for record in handlers.values():
        key = (record['entry']['section'], record['entry']['target_segment'],
               record['entry']['target_offset'])
        record['trace'] = calls.get(key) if calls is not None else None
        section_stats[key[0]]['object_handlers'] += 1
        section_stats[key[0]]['observed_object_handlers'] += int(key in call_keys)
    for record in callbacks:
        key = (record['entry']['section'], record['entry']['target_segment'],
               record['entry']['target_offset'])
        record['trace'] = calls.get(key) if calls is not None else None
        section_stats[key[0]]['room_callbacks'] += 1
        section_stats[key[0]]['observed_room_callbacks'] += int(key in call_keys)

    handler_calls = sum((calls or {}).get(key, {}).get('calls', 0) for key in entries)
    callback_keys = {tuple(item['entry'][field] for field in
                           ('section', 'target_segment', 'target_offset')) for item in callbacks}
    unexpected = sorted(call_keys - known_keys)
    unclassified_calls = sum(calls[key]['calls'] for key in unexpected) if calls else 0
    summary = dict(object_count=len(objects), objects_with_handlers=sum(bool(o['handler']) for o in objects),
                   distinct_handlers=len(handlers), reused_handler_ids=sum(
                       len(h['objects']) > 1 for h in handlers.values()),
                   repeated_object_assignments=sum(len(h['objects']) - 1
                                                   for h in handlers.values()),
                   handler_entry_addresses=len(set(entries)),
                   room_callbacks=len(callbacks),
                   callback_entries_also_object_handlers=len(callback_keys & set(entries)),
                   observed_handlers=sum(key in call_keys for key in entries) if calls is not None else None,
                   observed_callbacks=sum(key in call_keys for key in callback_keys) if calls is not None else None,
                   object_handler_invocations=handler_calls if calls is not None else None,
                   unclassified_rtlink_invocations=unclassified_calls if calls is not None else None)
    return dict(schema_version=1, summary=summary, census=call_header,
                handlers=sorted(handlers.values(), key=lambda item: item['handler']),
                callbacks=sorted(callbacks, key=lambda item: item['callback_id']),
                sections=[dict(section=sid, **stats) for sid, stats in sorted(section_stats.items())],
                unclassified_rtlink_targets=[dict(entry=map_entry(key, sections), **calls[key])
                                              for key in unexpected] if calls is not None else [])


def prefix_clusters(report, plan, length=32):
    """Byte-identical entry prefixes, *not* asserted function equivalences."""
    sections = {section['id']: section for section in plan['sections']}
    prefix_map = defaultdict(list)
    images = {}
    for record in report['handlers']:
        entry = record['entry']
        sid = entry['section']
        if sid not in images:
            section = sections[sid]
            data = Path(section['relocated_file']).read_bytes()
            if len(data) != section['file_bytes'] or hashlib.sha256(data).hexdigest() != \
                    section['relocated_sha256']:
                raise ValueError(f'section {sid}: relocated image fingerprint mismatch')
            images[sid] = data
        offset = entry['image_offset']
        prefix = images[sid][offset:offset+length]
        if len(prefix) == length:
            prefix_map[prefix].append(record['handler'])
    clusters = [dict(handler_ids=ids, count=len(ids),
                     prefix_sha256=hashlib.sha256(prefix).hexdigest())
                for prefix, ids in prefix_map.items() if len(ids) > 1]
    return sorted(clusters, key=lambda item: (-item['count'], item['handler_ids']))


def format_text(report):
    s = report['summary']
    out = [f"Native objects: {s['object_count']} ({s['objects_with_handlers']} with handlers)",
           f"Handler IDs: {s['distinct_handlers']} unique, "
           f"{s['handler_entry_addresses']} distinct section-qualified entry addresses",
           f"Shared handler IDs: {s['reused_handler_ids']} "
           f"({s['repeated_object_assignments']} extra object assignments); "
           f"callback entries: {s['room_callbacks']} "
           f"({s['callback_entries_also_object_handlers']} also object handlers)"]
    if report['census'] is None:
        out.append('Runtime coverage: not measured (no --rtlink-calls supplied)')
    else:
        c = report['census']
        out.extend([f"RTLink manager: {c['validated_calls']} validated trampoline calls "
                    f"to {c['distinct_targets']} targets; "
                    f"{c['unrecognized_manager_hits']} unrecognized hits",
                    f"Observed object entries: {s['observed_handlers']}/{s['distinct_handlers']} "
                    f"({s['object_handler_invocations']} calls); "
                    f"callbacks: {s['observed_callbacks']}/{s['room_callbacks']}",
                    f"Other RTLink calls: {s['unclassified_rtlink_invocations']} "
                    f"(not in either supplied table)"])
    out.append('Sections (object entries / observed, callback entries / observed):')
    for item in report['sections']:
        object_hits = (str(item['observed_object_handlers']) if report['census'] is not None else '?')
        callback_hits = (str(item['observed_room_callbacks']) if report['census'] is not None else '?')
        out.append(f"  {item['section']:2d}: {item['object_handlers']:3d} / "
                   f"{object_hits:>3}, {item['room_callbacks']:2d} / {callback_hits:>2}")
    if report['census'] is not None:
        observed = sorted((h for h in report['handlers'] if h['trace']),
                          key=lambda h: (-h['trace']['calls'], h['handler']))
        out.append('Invoked object handlers (count, ID, object name, original OVL offset):')
        for item in observed:
            name = ', '.join(obj['name'] or f"#{obj['id']}" for obj in item['objects'])
            out.append(f"  {item['trace']['calls']:5d}  #{item['handler']:3d} "
                       f"{name}: 0x{item['entry']['file_offset']:x}")
        others = sorted(report['unclassified_rtlink_targets'],
                        key=lambda item: -item['calls'])
        out.append('Highest-volume other RTLink entries (not object/callback IDs):')
        for item in others[:5]:
            out.append(f"  {item['calls']:5d}  section {item['entry']['section']:2d} "
                       f"OVL:0x{item['entry']['file_offset']:x}")
    if 'prefix_clusters' in report:
        out.append(f"Byte-identical 32-byte entry-prefix clusters: "
                   f"{len(report['prefix_clusters'])} (only candidates for review)")
        for item in report['prefix_clusters'][:12]:
            out.append(f"  {item['count']} entries: handler IDs "
                       + ','.join(str(h) for h in item['handler_ids']))
    return '\n'.join(out) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('objects_json', type=Path, help='inspect_object_state.py JSON snapshot')
    parser.add_argument('ghidra_plan', type=Path, help='ghidra-plan.json with relocated images')
    parser.add_argument('--callbacks-json', type=Path, help='inspect_room_callbacks.py JSON')
    parser.add_argument('--rtlink-calls', type=Path, help='probe --trace-rtlink-calls output')
    parser.add_argument('--format', choices=['text', 'json'], default='text')
    args = parser.parse_args()
    try:
        calls, header = (parse_calls(args.rtlink_calls.read_text().splitlines())
                         if args.rtlink_calls else (None, None))
        plan = json.loads(args.ghidra_plan.read_text())
        report = inventory(json.loads(args.objects_json.read_text()), plan,
                           json.loads(args.callbacks_json.read_text()) if args.callbacks_json else None,
                           calls, header)
        report['prefix_clusters'] = prefix_clusters(report, plan)
        print(json.dumps(report, indent=2) if args.format == 'json' else format_text(report),
              end='\n' if args.format == 'json' else '')
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
