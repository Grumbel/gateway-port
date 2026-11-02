#!/usr/bin/env python3
"""Contrast ordinary and controlled-state native room callback replays.

The controlled trace must contain one logged, compare-and-written 16-bit
guest RAM word. A successful native branch proves behavior *under this
intervention*, not that normal player input can produce the same world state.
"""

import argparse
from pathlib import Path

from summarize_navigation_trace import callback_return_entries, fields, location_events


def movement_pairs(lines):
    events = location_events(lines)
    if any(item['evidence'] != 'verified service' or item['old'] is None for item in events):
        raise ValueError('replay lacks corroborated native player-location writes')
    return [(item['old'], item['new'], item['ms']) for item in events]


def research_pokes(lines):
    result = []
    for line in lines:
        if not line.startswith('research-poke16 '):
            continue
        info = fields(line)
        try:
            result.append((int(info['ms']), int(info['address'], 16),
                           int(info['before'], 16), int(info['value'], 16)))
        except (KeyError, ValueError) as error:
            raise ValueError(f'invalid controlled research poke ({error}): {line}') from error
    return result


def compare_traces(baseline, controlled, callback_id, destination, expected_poke):
    if research_pokes(baseline):
        raise ValueError('baseline has a controlled guest-memory poke')
    if research_pokes(controlled) != [expected_poke]:
        raise ValueError('intervention trace has missing or unexpected research poke')
    ordinary = callback_return_entries(baseline)
    trial = callback_return_entries(controlled)
    if (len(ordinary) != 1 or len(trial) != 1 or
            any(item.get('callback_id') != callback_id or item.get('event') != 0x0f or
                item.get('section') is None or item.get('target_pointer') is None
                for item in ordinary + trial)):
        raise ValueError('expected one byte-verified room callback in each replay')
    before, after = ordinary[0], trial[0]
    if ((before['target_pointer'], before['section']) !=
            (after['target_pointer'], after['section'])):
        raise ValueError('RTLink handler mapping differs between replays')
    if before['target'] != 0 or after['target'] != destination:
        raise ValueError('room callback AX results do not match the expected branches')
    base_moves = movement_pairs(baseline)
    trial_moves = movement_pairs(controlled)
    if (not base_moves or len(trial_moves) != len(base_moves) + 1 or
            [(a, b) for a, b, _ in trial_moves[:len(base_moves)]] !=
            [(a, b) for a, b, _ in base_moves]):
        raise ValueError('pre-intervention navigation differs or other moves occurred')
    last = trial_moves[-1]
    if (last[0] != base_moves[-1][1] or last[1] != destination or
            not after['ms'] <= last[2] <= after['ms'] + 500):
        raise ValueError('callback return is not corroborated by a native destination move')
    if not base_moves[-1][2] < expected_poke[0] < after['ms']:
        raise ValueError('poke is not between previous movement and callback return')
    return dict(callback_id=callback_id, section=before['section'],
                target_pointer=before['target_pointer'], baseline_ax=before['target'],
                controlled_ax=after['target'], poke=expected_poke,
                baseline_movements=len(base_moves), controlled_movements=len(trial_moves),
                old_room=last[0], new_room=last[1], movement_ms=last[2])


def parse_poke(text):
    parts = text.split(':')
    if len(parts) != 4:
        raise ValueError('expected --poke MS:ADDRESS:OLD:NEW')
    try:
        result = (int(parts[0], 10), *(int(part, 16) for part in parts[1:]))
    except ValueError as error:
        raise ValueError('invalid decimal time or hexadecimal poke fields') from error
    if not 0 <= result[0] <= 60000 or not 0 <= result[1] <= 0x9fffe or \
            any(not 0 <= part <= 0xffff for part in result[2:]):
        raise ValueError('controlled research poke out of range')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline', type=Path, help='unmodified native.trace')
    parser.add_argument('controlled', type=Path, help='native.trace with one research poke')
    parser.add_argument('--callback', required=True, type=lambda s: int(s, 0))
    parser.add_argument('--destination', required=True, type=lambda s: int(s, 0))
    parser.add_argument('--poke', required=True, help='MS:ADDRESS:OLD:NEW')
    args = parser.parse_args()
    try:
        result = compare_traces(args.baseline.read_text().splitlines(),
                                args.controlled.read_text().splitlines(),
                                args.callback, args.destination, parse_poke(args.poke))
    except (OSError, ValueError, KeyError) as error:
        parser.error(str(error))
    print('Controlled native room callback branch comparison (not normal game progression):')
    print(f'  callback=0x{result["callback_id"]:02x} '
          f'section={result["section"]} pointer={result["target_pointer"]}')
    ms, address, old, new = result['poke']
    print(f'  guest RAM poke at {ms} ms: [0x{address:x}] 0x{old:04x} -> 0x{new:04x}')
    print(f'  native AX: baseline=0x{result["baseline_ax"]:04x}; '
          f'controlled=0x{result["controlled_ax"]:04x}')
    print(f'  verified player movements: {result["baseline_movements"]} -> '
          f'{result["controlled_movements"]}; added '
          f'0x{result["old_room"]:03x} -> 0x{result["new_room"]:03x} '
          f'@{result["movement_ms"]} ms')
    print('Interpretation: native code took both branches with an explicitly '
          'modified in-memory precondition; puzzle reachability is not established.')


if __name__ == '__main__':
    main()
