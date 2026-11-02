#!/usr/bin/env python3
"""Cross-check callback 0x2b's Terminal and Beach UP paths in original replays.

Both traces must come from normal typed commands with the original DOS files,
the player record watched (`--trace-object-writes d3`) and no guest pokes.
This verifies executed location writes and AX; it does not infer text rendering
or claim the glass flag-clear call changed a bit in the sampled game state.
"""

import argparse
from pathlib import Path

from inspect_beach_pin import inspect_snapshot
from summarize_navigation_trace import callback_return_entries, fields, location_events


CALLBACK_ID = 0x2b
SECTION = 50
ENTRY_PC = 0x49bec


def _exit_calls(lines, label):
    if any(line.startswith('research-poke16 ') for line in lines):
        raise ValueError(label + ' replay modified original guest memory')
    calls = [entry for entry in callback_return_entries(lines)
             if entry.get('callback_id') == CALLBACK_ID and entry.get('event') == 0x0f]
    if len(calls) != 2:
        raise ValueError(label + ' replay needs exactly two UP callback 0x2b calls')
    for call in calls:
        if (call['target'] != 0 or call['section'] != SECTION or
                not call['target_pointer']):
            raise ValueError(label + ' native callback AX or RTLink target differs')
        if not any(line.startswith('entry ') and 'pc=%x ' % ENTRY_PC in line and
                   'cs=4990 ' in line and 'ax=f ' in line and
                   call['call_ms'] <= int(fields(line)['ms']) <= call['ms']
                   for line in lines):
            raise ValueError(label + ' original section-50 event-0x0f entry not observed')
    if calls[0]['target_pointer'] != calls[1]['target_pointer']:
        raise ValueError(label + ' native callback trampolines differ')
    return calls


def _player_moves(lines):
    return [event for event in location_events(lines) if event['old'] is not None
            and event['evidence'] == 'verified service']


def _expect_move(moves, start, finish, old, new, label):
    within = [event for event in moves if start <= event['ms'] <= finish]
    if len(within) != 1 or within[0]['old'] != old or within[0]['new'] != new:
        raise ValueError(label + ' requires one watched native player write '
                         + '0x%03x -> 0x%03x' % (old, new))
    return within[0]['ms']


def _expect_no_move(moves, start, finish, label):
    if any(start <= event['ms'] <= finish for event in moves):
        raise ValueError(label + ' unexpectedly moved the player')


def _collar_locations(lines):
    result = []
    for line in lines:
        if line.startswith('object-location-set '):
            data = fields(line)
            if data.get('object') == '2a8' and data.get('changed') == '1':
                result.append((int(data['ms']), int(data['location'], 16)))
    return result


def verify_pair(terminal_lines, beach_lines, terminal_state, beach_state):
    """Validate two terminal exits and two Beach exits against original writes."""
    terminal_calls = _exit_calls(terminal_lines, 'VR Terminal')
    beach_calls = _exit_calls(beach_lines, 'Beach')
    if terminal_calls[0]['target_pointer'] != beach_calls[0]['target_pointer']:
        raise ValueError('VR Terminal and Beach use different callback trampolines')
    for label, state, wanted in (('VR Terminal', terminal_state, 0x29b),
                                  ('Beach', beach_state, 0x2ae)):
        for field in ('player_location', 'player_enclosing_room'):
            if state.get(field) != wanted:
                raise ValueError(label + ' final native %s is not 0x%03x' % (field, wanted))

    terminal_moves = _player_moves(terminal_lines)
    collared, uncollared = terminal_calls
    if not any(move['old'] == 0x29b and move['new'] == 0x2a1 and
               move['ms'] < collared['call_ms'] for move in terminal_moves):
        raise ValueError('VR Terminal missing watched couch-entry write')
    collars = _collar_locations(terminal_lines)
    if not any(ms < collared['call_ms'] and location == 0xd3 for ms, location in collars):
        raise ValueError('VR Terminal collar was not equipped before blocked UP')
    if not any(collared['ms'] < ms < uncollared['call_ms'] and location != 0xd3
               for ms, location in collars):
        raise ValueError('VR Terminal collar was not removed before successful UP')
    _expect_no_move(terminal_moves, collared['call_ms'], collared['ms'],
                    'equipped VR Terminal UP')
    terminal_rise = _expect_move(terminal_moves, uncollared['call_ms'], uncollared['ms'],
                                 0x2a1, 0x29b, 'unequipped VR Terminal UP')

    beach_moves = _player_moves(beach_lines)
    nested, direct = beach_calls
    if not any(move['old'] == 0x2ae and move['new'] == 0x2b1 and
               move['ms'] < nested['call_ms'] for move in beach_moves):
        raise ValueError('Beach missing watched nested-location entry')
    beach_rise = _expect_move(beach_moves, nested['call_ms'], nested['ms'],
                              0x2b1, 0x2ae, 'nested Beach UP')
    _expect_no_move(beach_moves, direct['call_ms'], direct['ms'], 'direct Beach UP')
    return dict(trampoline=collared['target_pointer'], section=SECTION,
                collar_blocked_ms=collared['ms'], terminal_rise_ms=terminal_rise,
                beach_rise_ms=beach_rise, beach_direct_ms=direct['ms'], returned_ax=0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('terminal', type=Path, help='original VR Terminal dump prefix')
    parser.add_argument('beach', type=Path, help='original Beach dump prefix')
    args = parser.parse_args()
    try:
        result = verify_pair(
            args.terminal.with_suffix('.trace').read_text().splitlines(),
            args.beach.with_suffix('.trace').read_text().splitlines(),
            inspect_snapshot(args.terminal), inspect_snapshot(args.beach))
    except (ValueError, KeyError, OSError) as error:
        parser.exit(1, 'inspect_vr_up: %s\n' % error)
    print('VERIFIED original VR Terminal and Beach UP: callback 0x2b, '
          'section 50, AX=0, no research pokes')
    print('  trampoline: %s' % result['trampoline'])
    print('  equipped collar: UP blocked at %d ms (no move)'
          % result['collar_blocked_ms'])
    print('  collar removed: player 0x2a1 -> 0x29b at %d ms'
          % result['terminal_rise_ms'])
    print('  nested Beach: player 0x2b1 -> 0x2ae at %d ms'
          % result['beach_rise_ms'])
    print('  direct Beach: UP leaves player in room at %d ms'
          % result['beach_direct_ms'])


if __name__ == '__main__':
    main()
