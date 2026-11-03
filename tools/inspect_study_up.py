#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Verify Study UP in original Gateway: natural refusal and controlled get-up.

The second trace intentionally changes *one* checked guest RAM word before UP.
It is NOT an unmodified-game successful playthrough. The first trace must
contain no pokes, and both must enter Study using ordinary typed commands.
"""

import argparse
from pathlib import Path
import struct

from inspect_beach_pin import inspect_snapshot
from summarize_navigation_trace import callback_return_entries, fields, location_events


STUDY = 0x2bc
COUCH = 0x2be
CALLBACK_ID = 0x2b
CALLBACK_SECTION = 50
ENTRY_PC = 0x49bec
COUNTER_PTR = 0x9634
COUNTER_OFFSET = 0x7904
COUNTER_PHYSICAL = 0x62d64  # DS 0x5b46 in these fingerprinted replays
POKE_MS = 74000


def inspect_study_snapshot(prefix):
    """Read signed Study word through the actual DS pointer, never guessed RAM."""
    state = inspect_snapshot(prefix)
    memory = prefix.with_suffix('.bin').read_bytes()
    ds = state['native_data_segment']
    pointer_at = ds * 16 + COUNTER_PTR
    if pointer_at + 2 > 0xa0000:
        raise ValueError('Study counter pointer is outside conventional RAM')
    segment = struct.unpack_from('<H', memory, pointer_at)[0]
    address = segment * 16 + COUNTER_OFFSET
    if not segment or address + 2 > 0xa0000:
        raise ValueError('Study counter segment points outside conventional RAM')
    state['study_counter'] = struct.unpack_from('<h', memory, address)[0]
    state['study_counter_address'] = address
    return state


def _verified_moves(lines):
    return [event for event in location_events(lines)
            if event['evidence'] == 'verified service' and event['old'] is not None]


def _study_calls(lines, label, count):
    matches = [call for call in callback_return_entries(lines)
               if call.get('callback_id') == CALLBACK_ID and call.get('event') == 0x0f]
    if len(matches) < count:
        raise ValueError(label + ' missing native callback 0x2b UP events')
    calls = matches[:count]
    for call in calls:
        if (call['target'] != 0 or call['section'] != CALLBACK_SECTION or
                call['target_pointer'] is None):
            raise ValueError(label + ' callback AX or RTLink section differs')
        if not any(line.startswith('entry ') and
                   ('pc=%x cs=4990 ' % ENTRY_PC) in line and
                   'ax=f ' in line and
                   call['call_ms'] <= int(fields(line)['ms']) <= call['ms']
                   for line in lines):
            raise ValueError(label + ' did not execute the original event-0x0f callback')
    if any(call['target_pointer'] != calls[0]['target_pointer'] for call in calls):
        raise ValueError(label + ' callback trampoline changed')
    return calls


def _in_window(moves, call):
    return [move for move in moves if call['call_ms'] <= move['ms'] <= call['ms']]


def _verify_couch_entry(moves, call, label):
    if not any(move['old'] == 0x29b and move['new'] == STUDY and
               move['ms'] < call['call_ms'] for move in moves):
        raise ValueError(label + ' lacks normal VR Terminal -> Study entry')
    if not any(move['old'] == STUDY and move['new'] == COUCH and
               move['ms'] < call['call_ms'] for move in moves):
        raise ValueError(label + ' lacks watched Study couch entry')
    preceding = [move for move in moves if move['ms'] < call['call_ms']]
    if (not preceding or preceding[-1]['old'] != STUDY or
            preceding[-1]['new'] != COUCH):
        raise ValueError(label + ' did not remain on the Study couch until UP')


def verify_pair(natural_lines, controlled_lines, natural_state, controlled_state):
    """Check the native blocked path separately from the audited positive path."""
    if any(line.startswith('research-poke16 ') for line in natural_lines):
        raise ValueError('natural Study replay includes a research poke')
    pokes = [fields(line) for line in controlled_lines
             if line.startswith('research-poke16 ')]
    if len(pokes) != 1 or pokes[0] != dict(ms=str(POKE_MS),
                                          address='%x' % COUNTER_PHYSICAL,
                                          before='1', value='3'):
        raise ValueError('controlled Study replay needs exactly one audited 1 -> 3 poke')
    if not any(line.startswith('byte-write ') and
               'address=%x ' % COUNTER_PHYSICAL in line and
               'pc=4bac7 ' in line and 'before=0 value=1' in line
               for line in controlled_lines):
        raise ValueError('missing original Study counter initialization write')
    if not any(line.startswith('byte-write ') and
               'address=%x ' % COUNTER_PHYSICAL in line and
               'before=3 value=4' in line for line in controlled_lines):
        raise ValueError('missing original post-get-up counter increment')

    for label, state, progress in (('natural', natural_state, 1),
                                   ('controlled', controlled_state, 4)):
        if (state.get('study_counter') != progress or
                state.get('study_counter_address') != COUNTER_PHYSICAL):
            raise ValueError(label + ' Study counter snapshot differs')
    if (controlled_state.get('player_location') != STUDY or
            controlled_state.get('player_enclosing_room') != STUDY):
        raise ValueError('controlled player did not finish in Study room')

    natural = _study_calls(natural_lines, 'natural', 1)[0]
    controlled = _study_calls(controlled_lines, 'controlled', 2)
    if natural['target_pointer'] != controlled[0]['target_pointer']:
        raise ValueError('natural and controlled callbacks use different trampolines')
    for label, lines, call in (('natural', natural_lines, natural),
                               ('controlled', controlled_lines, controlled[0])):
        _verify_couch_entry(_verified_moves(lines), call, label)
    if _in_window(_verified_moves(natural_lines), natural):
        raise ValueError('natural Study UP moved the player with counter 1')

    moves = _verified_moves(controlled_lines)
    rise = _in_window(moves, controlled[0])
    if len(rise) != 1 or rise[0]['old'] != COUCH or rise[0]['new'] != STUDY:
        raise ValueError('controlled Study UP lacks watched 0x2be -> 0x2bc write')
    if not natural['call_ms'] < natural['ms'] or not controlled[0]['call_ms'] < controlled[0]['ms']:
        raise ValueError('Study callbacks have no native execution interval')
    if not controlled[0]['ms'] < controlled[1]['call_ms']:
        raise ValueError('Study direct-room UP was not a subsequent action')
    if any(controlled[0]['ms'] < move['ms'] < controlled[1]['call_ms'] for move in moves):
        raise ValueError('player left Study between controlled UP callbacks')
    if _in_window(moves, controlled[1]):
        raise ValueError('direct Study UP unexpectedly moved the player')
    return dict(trampoline=natural['target_pointer'], natural_up_ms=natural['ms'],
                controlled_up_ms=controlled[0]['ms'], native_move_ms=rise[0]['ms'],
                direct_up_ms=controlled[1]['ms'], returned_ax=0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('natural', type=Path, help='unmodified Study UP dump prefix')
    parser.add_argument('controlled', type=Path, help='audited single-poke dump prefix')
    args = parser.parse_args()
    try:
        report = verify_pair(
            args.natural.with_suffix('.trace').read_text().splitlines(),
            args.controlled.with_suffix('.trace').read_text().splitlines(),
            inspect_study_snapshot(args.natural), inspect_study_snapshot(args.controlled))
    except (ValueError, KeyError, OSError, struct.error) as error:
        parser.exit(1, 'inspect_study_up: %s\n' % error)
    print('VERIFIED original Study callback 0x2b, section 50, '
          'trampoline %s, AX=0' % report['trampoline'])
    print('  Natural UP with counter=1 at %d ms: no move' % report['natural_up_ms'])
    print('  Audited RAM change at %d ms: counter 1 -> 3 (NOT ordinary gameplay)' % POKE_MS)
    print('  Controlled UP at %d ms: player couch 0x2be -> Study 0x2bc at %d ms'
          % (report['controlled_up_ms'], report['native_move_ms']))
    print('  Direct room UP at %d ms: no move; native counter reaches 4'
          % report['direct_up_ms'])


if __name__ == '__main__':
    main()
