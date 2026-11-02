#!/usr/bin/env python3
"""Verify both *unmodified* original Tanning Room UP callback paths.

Room 0x29a's UP exit uses kind-4 callback 0x2b at OVL:0x693fc. Event 0x0f
returns AX=0 in both branches, but when the player directly occupies couch
0x29c it moves the player back into the room before returning. The input
snapshots are from two separate normal-keyboard Unicorn replays.
"""

import argparse
from pathlib import Path

from inspect_beach_pin import inspect_snapshot
from summarize_navigation_trace import callback_return_entries, location_events


TANNING_ROOM = 0x29a
COUCH = 0x29c
CALLBACK_ID = 0x2b
CALLBACK_SECTION = 50
CALLBACK_ENTRY_PC = 0x49bec


def verify_pair(standing, reclining, standing_state, reclining_state):
    """Check native path selection and real player writes, not only end-state."""
    for label, lines, state in (('standing', standing, standing_state),
                                ('reclining', reclining, reclining_state)):
        if any(line.startswith('research-poke16 ') for line in lines):
            raise ValueError(label + ' replay modified original guest memory')
        for field, wanted in (('player_location', TANNING_ROOM),
                              ('player_enclosing_room', TANNING_ROOM),
                              ('pin_carried', True), ('beach_success_byte', 1),
                              ('drink_refresh_counter', 5)):
            if state.get(field) != wanted:
                raise ValueError('%s final native %s is not %s' % (label, field, wanted))

    def exit_call(lines, label):
        matches = [entry for entry in callback_return_entries(lines)
                   if entry.get('callback_id') == CALLBACK_ID and entry.get('event') == 0x0f]
        if len(matches) != 1:
            raise ValueError(label + ' replay needs one room-exit callback 0x2b')
        call = matches[0]
        if (call['target'] != 0 or call['section'] != CALLBACK_SECTION or
                call['target_pointer'] is None):
            raise ValueError(label + ' native callback AX/RTLink target differs')
        if not any(line.startswith('entry ') and
                   ('pc=%x ' % CALLBACK_ENTRY_PC) in line and
                   'cs=4990 ' in line and
                   call['call_ms'] <= int(line.split('ms=')[1].split()[0]) <= call['ms']
                   for line in lines):
            raise ValueError(label + ' original section-50 callback entry not observed')
        return call

    before = exit_call(standing, 'standing')
    after = exit_call(reclining, 'reclining')
    if before['target_pointer'] != after['target_pointer']:
        raise ValueError('the two replays used different native callback trampolines')

    standing_moves = [event for event in location_events(standing)
                      if event['evidence'] == 'verified service' and
                      before['call_ms'] <= event['ms'] <= before['ms'] + 500]
    if standing_moves:
        raise ValueError('standing UP unexpectedly changed player location')

    reclining_moves = [event for event in location_events(reclining)
                       if event['evidence'] == 'verified service' and
                       event['old'] is not None]
    lie = [event for event in reclining_moves if event['old'] == TANNING_ROOM and
           event['new'] == COUCH and event['ms'] < after['call_ms']]
    rise = [event for event in reclining_moves if event['old'] == COUCH and
            event['new'] == TANNING_ROOM and
            after['call_ms'] <= event['ms'] <= after['ms']]
    if len(lie) != 1 or len(rise) != 1:
        raise ValueError('missing watched native couch-entry or get-up movement')
    return dict(callback_id=CALLBACK_ID, section=CALLBACK_SECTION,
                trampoline=before['target_pointer'], standing_up_ms=before['ms'],
                lie_ms=lie[0]['ms'], reclining_up_ms=after['ms'],
                native_get_up_ms=rise[0]['ms'], returned_ax=0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('standing', type=Path, help='standing-UP native dump prefix')
    parser.add_argument('reclining', type=Path, help='LIE ON COUCH then UP dump prefix')
    args = parser.parse_args()
    try:
        result = verify_pair(
            args.standing.with_suffix('.trace').read_text().splitlines(),
            args.reclining.with_suffix('.trace').read_text().splitlines(),
            inspect_snapshot(args.standing), inspect_snapshot(args.reclining))
    except (ValueError, OSError, KeyError) as error:
        parser.exit(1, 'inspect_tanning_up: %s\n' % error)
    print('VERIFIED native Tanning UP, no research memory modifications:')
    print('  callback=0x%02x, section=%d, trampoline=%s; both return AX=0'
          % (result['callback_id'], result['section'], result['trampoline']))
    print('  standing UP: no player move at %d ms' % result['standing_up_ms'])
    print('  LIE: player 0x29a → couch 0x29c at %d ms' % result['lie_ms'])
    print('  reclining UP: player 0x29c → room 0x29a at %d ms; return at %d ms'
          % (result['native_get_up_ms'], result['reclining_up_ms']))


if __name__ == '__main__':
    main()
