#!/usr/bin/env python3
"""Join Gateway native handler IDs, selected Ghidra call sites and RTLink replays.

This is an evidence index and a *triage* heuristic, not a call graph inferred
from executed instructions or a percentage of story logic recovered.
"""
import argparse
from collections import Counter, defaultdict
import json
import math
from pathlib import Path

from inspect_rtlink_events import parse_events
from inventory_native_handlers import parse_calls


def entry_key(entry):
    return entry['section'], entry['target_segment'], entry['target_offset']


def build_index(inventory, graph, replays):
    if inventory.get('schema_version') != 1 or \
            graph.get('schema_version') != 1 or not isinstance(graph.get('targets'), list):
        raise ValueError('unsupported inventory or Ghidra callgraph schema')
    targets = {}
    offsets = {}

    def register(entry, label, details):
        key = entry_key(entry)
        if key in targets:
            raise ValueError('conflicting section-qualified native entry')
        offset = entry['file_offset']
        if offset in offsets:
            raise ValueError('duplicate original OVL offset across native entries')
        offsets[offset] = key
        targets[key] = dict(kind=label, entry=entry, **details)

    for item in inventory['handlers']:
        register(item['entry'], 'object-handler',
                 dict(handler_id=item['handler'], objects=item['objects']))
    for item in inventory['callbacks']:
        register(item['entry'], 'room-callback',
                 dict(callback_id=item['callback_id'], room_exit_uses=item['uses']))
    for item in inventory.get('unclassified_rtlink_targets', []):
        key = entry_key(item['entry'])
        if key not in targets:
            register(item['entry'], 'other-rtlink', {})

    static = defaultdict(list)
    inbound = defaultdict(list)
    for item in graph['targets']:
        origin = item.get('origin', '')
        if not origin.startswith('GATEMAIN.OVL:0x'):
            continue  # Resident MZ addresses are not original OVL offsets.
        try:
            offset = int(origin.split(':0x')[1], 16)
        except ValueError as error:
            raise ValueError('invalid Ghidra source offset') from error
        if not isinstance(item.get('edges'), list) or \
                not isinstance(item.get('unresolved_calls'), list):
            raise ValueError('Ghidra callgraph target lacks a call-reference list')
        static[offset].append(item)
        for edge in item['edges']:
            destination = edge.get('destination_ovl_file_offset')
            if destination is not None:
                inbound[destination].append(dict(source=item['name'],
                                                 source_offset=offset,
                                                 callsite=edge['callsite']))

    replay_summaries = []
    calls_by_key = Counter()
    events_by_key = defaultdict(list)
    for name, calls, header, events, event_summary in replays:
        if event_summary['selected'] != len(events):
            raise ValueError('RTLink sample footer mismatches events')
        seen = Counter()
        for event in events:
            key = event['section'], *event['target']
            seen[key] += 1
            event_data = dict(replay=name, id=event['id'], ax_in=event['ax_in'],
                              ax_out=event['ax_out'], args=event['args'],
                              start_ms=event['start_ms'], returned=event['returned'],
                              changed_objects=sorted({d['object'] for d in event['changes']}),
                              changed_bytes=len(event['changes']),
                              object_state=event.get('object_after', event['object_before']))
            events_by_key[key].append(event_data)
        if any(count > calls.get(key, {}).get('calls', 0)
               for key, count in seen.items()):
            raise ValueError(f'{name}: sampled events exceed the RTLink census')
        for key, record in calls.items():
            calls_by_key[key] += record['calls']
        replay_summaries.append(dict(name=name, **header, sampled=event_summary['selected'],
                                     returns=event_summary['returned'],
                                     unfinished=event_summary['unfinished']))

    result = []
    for key, target in targets.items():
        offset = target['entry']['file_offset']
        source = static.get(offset, [])
        sample = events_by_key.get(key, [])
        changes = sum(event['changed_bytes'] for event in sample)
        observed_calls = calls_by_key.get(key, 0)
        unique_inputs = len({(e['ax_in'], tuple(e['args'])) for e in sample})
        # Review order, not probability or fraction of game logic recovered.
        priority = (8 if changes else 0) + (4 if sample else 0) + \
                   min(5, int(math.log2(observed_calls + 1))) + \
                   min(3, len(inbound.get(offset, [])))
        result.append(dict(**target, observed_rtlink_calls=observed_calls,
                           sampled_events=len(sample),
                           paired_returns=sum(e['returned'] for e in sample),
                           observed_input_shapes=unique_inputs,
                           changed_object_bytes=changes,
                           changed_objects=sorted({obj for e in sample
                                                   for obj in e['changed_objects']}),
                           native_samples=sample,
                           ghidra_contexts=[dict(name=s['name'],
                                                 function_entry=s['function_entry'],
                                                 static_call_references=len(s['edges']),
                                                 unresolved_call_sites=len(s['unresolved_calls']),
                                                 outgoing=s['edges']) for s in source],
                           static_incoming=inbound.get(offset, []),
                           triage_score=priority))
    # An observed target absent from the inventory is retained as an explicitly
    # unclassified runtime entry. Mapping it to an OVL file offset requires a
    # verified plan, and is not invented here.
    unknown = [dict(section=key[0], target_segment=key[1], target_offset=key[2],
                    calls=count)
               for key, count in calls_by_key.items() if key not in targets]
    result.sort(key=lambda item: (-item['triage_score'],
                                  -item['observed_rtlink_calls'],
                                  item['entry']['file_offset']))
    return dict(schema_version=1,
                summary=dict(handler_entries=sum(t['kind'] == 'object-handler'
                                                 for t in result),
                             room_callbacks=sum(t['kind'] == 'room-callback' for t in result),
                             replays=len(replays),
                             sampled_events=sum(len(v) for v in events_by_key.values()),
                             observed_known_entries=sum(t['observed_rtlink_calls'] > 0
                                                        for t in result),
                             ghidra_matched_entries=sum(bool(t['ghidra_contexts'])
                                                       for t in result),
                             unknown_census_targets=len(unknown)),
                replays=replay_summaries, targets=result, unknown_rtlink_targets=unknown)


def format_text(report, max_items=20):
    summary = report['summary']
    lines = [f"Native evidence index: {summary['handler_entries']} object handlers, "
             f"{summary['room_callbacks']} room callbacks; {summary['replays']} replays",
             f"Known RTLink entries observed: {summary['observed_known_entries']} "
             f"(not branch coverage); sampled calls: {summary['sampled_events']}",
             f"Ghidra contexts matched to known entries: {summary['ghidra_matched_entries']}",
             'Triage (score is a heuristic; static references are not executed calls):']
    for item in report['targets'][:max_items]:
        entry = item['entry']
        identifier = (f"handler #{item['handler_id']}" if item['kind'] == 'object-handler'
                      else f"callback #{item['callback_id']}" if item['kind'] == 'room-callback'
                      else 'other RTLink target')
        lines.append(f"  {item['triage_score']:2d}  {identifier}, section {entry['section']} "
                     f"OVL 0x{entry['file_offset']:x}: "
                     f"calls={item['observed_rtlink_calls']} "
                     f"samples={item['sampled_events']} "
                     f"changed-bytes={item['changed_object_bytes']} "
                     f"static-contexts={len(item['ghidra_contexts'])}")
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inventory', type=Path, help='inventory_native_handlers.py --format json')
    parser.add_argument('callgraph', type=Path, help='ghidra-callgraph.json')
    parser.add_argument('--replay', nargs=3, action='append', default=[],
                        metavar=('NAME', 'RTLINK-CALLS', 'RTLINK-EVENTS'),
                        help='paired census and sampled events from one native replay (repeatable)')
    parser.add_argument('--format', choices=('text', 'json'), default='text')
    parser.add_argument('--limit', type=int, default=20, help='number of triage rows in text mode')
    args = parser.parse_args()
    try:
        replays = []
        for name, census_path, event_path in args.replay:
            calls, header = parse_calls(Path(census_path).read_text().splitlines())
            events, event_summary = parse_events(Path(event_path).read_text().splitlines())
            replays.append((name, calls, header, events, event_summary))
        report = build_index(json.loads(args.inventory.read_text()),
                             json.loads(args.callgraph.read_text()), replays)
        print(json.dumps(report, indent=2) if args.format == 'json'
              else format_text(report, max_items=max(0, args.limit)),
              end='\n' if args.format == 'json' else '')
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f'build_handler_evidence: {error}\n')


if __name__ == '__main__':
    main()
