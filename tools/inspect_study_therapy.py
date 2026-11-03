#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Verify unmodified native Study therapy: 19 stages and the desk DOWN gate.

Requires the two documented original-keyboard puzzle replays, player-location
word watch, Study counter-byte watch, and navigation entry hooks. The script
does not modify guest RAM or infer story success from typed text alone.
"""

import argparse
from pathlib import Path

from inspect_study_up import COUNTER_PHYSICAL, _verified_moves, inspect_study_snapshot
from summarize_navigation_trace import callback_return_entries, fields
from story_scenarios import PUZZLE_FIRST_COMMAND_MS, PUZZLE_COMMAND_SPACING_MS


STUDY = 0x2bc
COUCH = 0x2be
DESK = 0x2bf
ENTRY_UP = 0x49bec
ENTRY_DOWN = 0x49d7d
ORIGINAL_INC = 0x4bc21
ORIGINAL_INIT = 0x4bac7
PHASE_ACTIONS = (
    'scream', 'feel couch', 'up', 'hop', 'hit myself', 'step on hand',
    'smell hand', 'climb walls', 'jump on desk', 'sing', 'dance', 'dance',
    'down', 'attack couch', 'clean desk', 'cry', 'rub hand', 'kiss floor',
    'sit on couch',
)


def _slot(stage, blocked):
    # First 18 slots enter VR and ask to LOOK; the action for stage 1 is
    # slot 19. The desk-gate replay inserts a deliberately premature DOWN
    # at slot 28, shifting all subsequent stages by one.
    return 18 + stage + (1 if blocked and stage >= 10 else 0)


def _in_slot(ms, index):
    start = PUZZLE_FIRST_COMMAND_MS + index * PUZZLE_COMMAND_SPACING_MS
    return start <= ms < start + PUZZLE_COMMAND_SPACING_MS


def _counter_writes(lines):
    result = []
    for line in lines:
        if not line.startswith('byte-write '):
            continue
        info = fields(line)
        if int(info['address'], 16) != COUNTER_PHYSICAL:
            continue
        if int(info['size'], 16) != 2:
            raise ValueError('native Study counter watch has wrong write size')
        result.append(dict(ms=int(info['ms']), pc=int(info['pc'], 16),
                           old=int(info['before'], 16), new=int(info['value'], 16)))
    return result


def _callbacks(lines, callback_id, entry_pc, expected_count):
    matches = [call for call in callback_return_entries(lines)
               if call.get('callback_id') == callback_id and call.get('event') == 0x0f]
    if len(matches) != expected_count:
        raise ValueError('wrong number of original callback 0x%02x events' % callback_id)
    for call in matches:
        if (call['target'] != 0 or call['section'] != 50 or
                call['target_pointer'] != ('2c24:13f3' if callback_id == 0x2b
                                           else '2c24:156f')):
            raise ValueError('callback 0x%02x native AX or trampoline differs' % callback_id)
        # These replays explicitly hook DOWN's original entry 0x6958d;
        # callback 0x2b's entry 0x693fc was separately hooked and checked
        # in the earlier Study UP verifier. Its RTLink trampoline and
        # native scanner return are still required here.
        if callback_id == 0x05 and not any(
                line.startswith('entry ') and
                ('pc=%x cs=4990 ' % entry_pc) in line and 'ax=f ' in line and
                call['call_ms'] <= int(fields(line)['ms']) <= call['ms']
                for line in lines):
            raise ValueError('callback 0x%02x original event entry missing' % callback_id)
    return matches


def _in_callback(moves, call):
    return [move for move in moves if call['call_ms'] <= move['ms'] <= call['ms']]


def _assert_move(moves, old, new, slot):
    matching = [move for move in moves if _in_slot(move['ms'], slot)]
    if len(matching) != 1 or matching[0]['old'] != old or matching[0]['new'] != new:
        raise ValueError('missing watched native player move 0x%x -> 0x%x in slot %d'
                         % (old, new, slot))
    return matching[0]


def _verify(lines, state, *, blocked):
    if any(line.startswith('research-poke16 ') for line in lines):
        raise ValueError('Study therapy must be unmodified native gameplay')
    expected_progress = 14 if blocked else 20
    expected_location = STUDY if blocked else COUCH
    if (state.get('study_counter') != expected_progress or
            state.get('study_counter_address') != COUNTER_PHYSICAL or
            state.get('player_location') != expected_location or
            state.get('player_enclosing_room') != STUDY):
        raise ValueError('native Study therapy final counter/player state differs')
    original = _counter_writes(lines)
    init = [write for write in original if write['pc'] == ORIGINAL_INIT]
    increments = [write for write in original if write['pc'] == ORIGINAL_INC]
    if (len(init) != 1 or init[0]['old'] != 0 or init[0]['new'] != 1 or
            not _in_slot(init[0]['ms'], 18)):
        raise ValueError('native Study initial 0 -> 1 counter write missing')
    if len(increments) != expected_progress - 1:
        raise ValueError('missing or extra original Study stage increments')
    for stage, write in enumerate(increments, 1):
        if (write['old'] != stage or write['new'] != stage + 1 or
                not _in_slot(write['ms'], _slot(stage, blocked))):
            raise ValueError('Study stage %d counter advance is not native/timed' % stage)
    if [w for w in original if w['ms'] >= init[0]['ms']] != init + increments:
        raise ValueError('unaccounted native Study counter writes after initialization')

    moves = _verified_moves(lines)
    if not any(m['old'] == 0x29b and m['new'] == STUDY and
               m['ms'] < init[0]['ms'] for m in moves):
        raise ValueError('missing original VR entry into Study')
    if not any(m['old'] == STUDY and m['new'] == COUCH and
               m['ms'] < init[0]['ms'] for m in moves):
        raise ValueError('missing initial Study couch entry')
    rise = _assert_move(moves, COUCH, STUDY, _slot(3, blocked))
    climb = _assert_move(moves, STUDY, DESK, _slot(9, blocked))
    descend = _assert_move(moves, DESK, STUDY, _slot(13, blocked))
    if not rise['ms'] < climb['ms'] < descend['ms']:
        raise ValueError('Study get-up/desk/return movement order differs')

    up_calls = _callbacks(lines, 0x2b, ENTRY_UP, 1 if blocked else 2)
    down_calls = _callbacks(lines, 0x05, ENTRY_DOWN, 2 if blocked else 1)
    if (not _in_slot(up_calls[0]['ms'], _slot(3, blocked)) or
            _in_callback(moves, up_calls[0]) != [rise]):
        raise ValueError('Study UP callback did not perform the original rise')
    successful_down = down_calls[-1]
    if (not _in_slot(successful_down['ms'], _slot(13, blocked)) or
            _in_callback(moves, successful_down) != [descend]):
        raise ValueError('Study DOWN callback did not perform the original desk descent')
    if (not up_calls[0]['ms'] <= increments[2]['ms'] or
            not successful_down['ms'] <= increments[12]['ms']):
        raise ValueError('therapy advanced before its required native exit callback')

    if blocked:
        refusal = down_calls[0]
        if (not _in_slot(refusal['ms'], 28) or
                _in_callback(moves, refusal) or
                not refusal['ms'] < increments[9]['ms']):
            raise ValueError('early Study desk DOWN was not refused at progress 10')
        if any(_in_slot(move['ms'], 28) for move in moves):
            raise ValueError('premature DOWN unexpectedly moved the player')
    else:
        sit = _assert_move(moves, STUDY, COUCH, _slot(19, False))
        last_up = up_calls[1]
        if (not _in_slot(last_up['ms'], 39) or _in_callback(moves, last_up) or
                not sit['ms'] < last_up['call_ms'] or
                not increments[-1]['ms'] < last_up['call_ms']):
            raise ValueError('final Study UP was not blocked after stage 19')
    return dict(stages=expected_progress - 1,
                rise_ms=rise['ms'], climb_ms=climb['ms'], descend_ms=descend['ms'],
                counter=expected_progress)


def verify_pair(full_lines, gate_lines, full_state, gate_state):
    """Prove 19 natural advances plus a separate 10-locked/13-unlocked DOWN."""
    full = _verify(full_lines, full_state, blocked=False)
    gate = _verify(gate_lines, gate_state, blocked=True)
    return dict(full=full, gate=gate)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('full', type=Path, help='unmodified 19-stage Study replay prefix')
    parser.add_argument('desk_gate', type=Path,
                        help='unmodified replay attempting early DOWN at progress 10')
    args = parser.parse_args()
    try:
        result = verify_pair(args.full.with_suffix('.trace').read_text().splitlines(),
                             args.desk_gate.with_suffix('.trace').read_text().splitlines(),
                             inspect_study_snapshot(args.full),
                             inspect_study_snapshot(args.desk_gate))
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, 'inspect_study_therapy: %s\n' % error)
    print('VERIFIED 19 original Study therapy stages, no guest memory interventions:')
    print('  counter 0 -> 1 -> ... -> 20, original entry 0x6ce85')
    print('  couch -> Study at %d ms, Study -> desk at %d ms, '
          'desk -> Study at %d ms' % tuple(result['full'][k]
                                           for k in ('rise_ms', 'climb_ms', 'descend_ms')))
    print('  desk DOWN at progress 10 refused; at progress 13 native player move '
          'at %d ms, callback AX=0' % result['gate']['descend_ms'])
    print('  stage 19: player returns to couch, counter 20 and further UP is refused')


if __name__ == '__main__':
    main()
