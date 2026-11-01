#!/usr/bin/env python3
"""Align Gateway's native child-slot decisions with nearby object-state calls.

The first-room OPEN/CLOSE/T(AKE) experiment has separate probes for the
container eligibility check, its indexed child accessor, and the original
flag/location setter entries. A setter call does NOT prove changed bytes;
this report only aligns independently observed callsites by guest time.
"""
import argparse
from bisect import bisect_left
from pathlib import Path

from summarize_action_effects import parse_entry
from summarize_visibility_trace import parse_visibility


SERVICE_SITES = {0x11950: 'set', 0x11983: 'clear', 0x11bbb: 'location-set'}


def collect(lines, container, child):
    """Return timestamped gate samples and relevant native service entries."""
    samples = []
    services = []
    for line in lines:
        event = parse_visibility(line)
        if event is not None and not event['unverified'] and \
                event['object'] == container and event['slot'] == 0 and \
                event['restricted']:
            if event['stage'] == 'gate-return':
                if event['result'] not in (0, 1):
                    raise ValueError('non-Boolean native child-slot gate result')
                samples.append(dict(ms=event['ms'], gate=event['result'], child=None))
            elif event['stage'] == 'child-return' and samples:
                last = samples[-1]
                if last['gate'] == 1 and last['child'] is None and \
                        0 <= event['ms'] - last['ms'] <= 2:
                    last['child'] = event['result']
        entry = parse_entry(line)
        if entry is None or entry['cs'] != 0x1157 or \
                entry['pc'] not in SERVICE_SITES or len(entry['stack']) < 4:
            continue
        operation = SERVICE_SITES[entry['pc']]
        object_id, argument = entry['stack'][2:4]
        if ((object_id == container and argument == 0x0c and
             operation in ('set', 'clear')) or
                (object_id == child and operation == 'location-set')):
            services.append(dict(ms=entry['ms'], operation=operation,
                                 object=object_id, argument=argument))
    return samples, services


def summarize(lines, container, child, max_gap_ms=500):
    if not 0 < container <= 0xffff or not 0 < child <= 0xffff or container == child:
        raise ValueError('distinct nonzero 16-bit container and child IDs required')
    if not 0 < max_gap_ms <= 10000:
        raise ValueError('maximum correlation gap must be within 1–10000 milliseconds')
    samples, services = collect(lines, container, child)
    if not samples:
        raise ValueError('no verified restricted slot-0 samples for container')
    if not services:
        raise ValueError('no native flag-0x0c or child-location service entries')
    samples.sort(key=lambda sample: sample['ms'])
    timestamps = [sample['ms'] for sample in samples]
    output = ['ms     native service          before gate / child       after gate / child',
              '------  ----------------------  ------------------------  ------------------------']

    def format_sample(sample):
        if sample is None:
            return '-'
        allowed = 'allow' if sample['gate'] else 'deny'
        child_name = ('-' if sample['gate'] == 0 else
                      '?' if sample['child'] is None else f'0x{sample["child"]:03x}')
        return f'ms={sample["ms"]} {allowed} {child_name}'

    for service in services:
        position = bisect_left(timestamps, service['ms'])
        before = samples[position - 1] if position else None
        after = samples[position] if position < len(samples) else None
        if before is not None and service['ms'] - before['ms'] > max_gap_ms:
            before = None
        if after is not None and after['ms'] - service['ms'] > max_gap_ms:
            after = None
        operation = (f'{service["operation"]}(0x{service["object"]:03x},'
                     f'0x{service["argument"]:02x})')
        output.append(f'{service["ms"]:6}  {operation:<22}  '
                      f'{format_sample(before):<24}  {format_sample(after)}')
    output.append('Timings pair nearby native calls, not verified state deltas or '
                  'proof of successful parser command resolution.')
    return '\n'.join(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--container', required=True, type=lambda arg: int(arg, 0),
                        help='container object ID, e.g. 0x124 (drawer)')
    parser.add_argument('--child', required=True, type=lambda arg: int(arg, 0),
                        help='child object ID, e.g. 0x123 (book)')
    parser.add_argument('--max-gap-ms', type=int, default=500,
                        help='maximum interval from a native state call to a gate sample')
    args = parser.parse_args()
    try:
        print(summarize(args.trace.read_text().splitlines(), args.container, args.child,
                        args.max_gap_ms))
    except (OSError, ValueError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
