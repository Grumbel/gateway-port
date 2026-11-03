#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Summarize Gateway's resident action-selector stage before final dispatch.

This is a timing association between scripted input slots and instruction
snapshots. The selector callback's AX value has NOT been proven to be an
allow/deny Boolean, and a missing dispatch does not prove parser rejection.
"""
import argparse
from pathlib import Path

from inspect_action_dispatch import KNOWN_ACTIONS


SLOTS = (18, 21, 25, 28, 31, 34, 37)
SELECTOR_ENTRY = 0x14768    # 1476:0008, original EXE file 0x5956
SELECTOR_CALLBACK = 0x14799  # 1476:0039, original EXE file 0x5987
SELECTOR_RETURN = 0x147a2    # 1476:0042, original EXE file 0x5990
ACTION_DISPATCH = 0x147c0    # 1476:0060, original EXE file 0x59ae
ACTION_IDS = {name: value for value, name in KNOWN_ACTIONS.items()}


def parse_entry(line):
    if not line.startswith('entry '):
        return None
    fields = dict(part.split('=', 1) for part in line.split()[1:] if '=' in part)
    try:
        return dict(ms=int(fields['ms']), pc=int(fields['pc'], 16),
                    cs=int(fields['cs'], 16), fields=fields)
    except (KeyError, ValueError):
        return None


def collect(lines):
    """Associate entry/callback/return events within each live selector call."""
    active = []
    selectors = []
    dispatches = []
    for line in lines:
        entry = parse_entry(line)
        if not entry or entry['cs'] != 0x1476:
            continue
        pc, fields = entry['pc'], entry['fields']
        if pc == SELECTOR_ENTRY and 'admission_action' in fields:
            try:
                action = int(fields['admission_action'], 16)
            except ValueError:
                continue
            active.append(dict(ms=entry['ms'], action=action, selector=None,
                               target=None, returned=None))
        elif pc == SELECTOR_CALLBACK and active and 'admission_selector' in fields:
            try:
                active[-1]['selector'] = int(fields['admission_selector'], 16)
                active[-1]['target'] = fields.get('admission_target')
            except ValueError:
                continue
        elif pc == SELECTOR_RETURN and active and 'admission_return' in fields:
            try:
                active[-1]['returned'] = int(fields['admission_return'], 16)
            except ValueError:
                continue
            selectors.append(active.pop())
        elif pc == ACTION_DISPATCH and 'stack' in fields:
            try:
                stack = [int(word, 16) for word in fields['stack'].split(',') if word]
                if len(stack) > 3:
                    dispatches.append(dict(ms=entry['ms'], action=stack[3]))
            except ValueError:
                continue
    return selectors, dispatches


def summarize(lines, commands):
    if len(commands) != len(SLOTS):
        raise ValueError('expected seven scripted commands')
    selectors, dispatches = collect(lines)
    if not selectors:
        raise ValueError('no completed selector-stage snapshots: check --trace-admission')

    def slot_of(ms):
        if ms < 17500:
            return None
        return min(SLOTS, key=lambda slot: abs(ms - 1000 * slot))

    rows = ['| Guest slot | Command | Selector calls (raw AX) | Final dispatch |',
            '| ---: | --- | --- | --- |']
    for slot, command in zip(SLOTS, commands):
        action = ACTION_IDS.get(command)
        events = [event for event in selectors if slot_of(event['ms']) == slot and
                  event['action'] == action]
        observations = []
        for event in events:
            if event['selector'] is None:
                info = 'no callback'
            else:
                info = (f"selector=0x{event['selector']:02x} -> "
                        f"{event['target'] or '?'}")
            info += f" AX=0x{event['returned']:x}"
            observations.append(info)
        matching = [event for event in dispatches if slot_of(event['ms']) == slot and
                    event['action'] == action]
        rows.append(f"| {slot}s | {command} | {', '.join(observations) or '—'} | "
                    f"{'observed' if matching else 'not observed'} |")
    return '\n'.join(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--commands', required=True,
                        help='exact seven comma-separated commands used in this replay')
    args = parser.parse_args()
    try:
        print(summarize(args.trace.read_text().splitlines(), args.commands.split(',')))
    except ValueError as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
