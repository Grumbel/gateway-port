#!/usr/bin/env python3
"""Verify Study refusal and terminal SLEEP using unmodified native state watches.

Supply two independent original-keyboard puzzle replays, both watching guest
physical bytes 0x62d64..0x62d69: one repeats LOOK after the first prompt;
the other completes the 19 exercises and types SLEEP as terminal stage 20.
This is an execution-path check, not a story VM or guest-RAM intervention.
"""

import argparse
from pathlib import Path
import struct

from inspect_study_up import _verified_moves, inspect_study_snapshot
from inspect_study_therapy import _in_slot
from summarize_navigation_trace import fields


BASE = 0x62d64
COUNTER = BASE
PREVIOUS_PROMPT = BASE + 2
REMINDER = BASE + 4
MATCHED = BASE + 5
STUDY_RESPONSE = 0x4bab5        # OVL 0x6ce85
INIT = 0x4bac7                  # OVL 0x6ce97
PROMPT_SAVED = 0x4bc04          # OVL 0x6cfd4
REMINDER_SET = 0x4bb7c          # OVL 0x6cf4c
INSISTENCE = 0x4bb85            # OVL 0x6cf55
PRIMARY_MATCH = 0x4a370        # OVL 0x6b740
ALTERNATE_MATCH = 0x4a3e7      # OVL 0x6b7b7
POSTURE_MATCH = 0x4bb1c        # OVL 0x6ceec
ADVANCE = 0x4bc21               # OVL 0x6cff1
MATCH_CLEAR = 0x4bc4c           # OVL 0x6d01c
PROMPT_CLEAR = 0x4bc56          # OVL 0x6d026
AUXILIARY = 0x4c194            # OVL 0x6d564


def inspect_response_snapshot(prefix):
    """Follow the actual DS segment pointers; check the stage-20 table tail."""
    state = inspect_study_snapshot(prefix)
    memory = prefix.with_suffix('.bin').read_bytes()
    ds = state['native_data_segment']

    def word(linear):
        if not 0 <= linear <= 0xa0000 - 2 or len(memory) < linear + 2:
            raise ValueError('Study response word outside native guest RAM')
        return struct.unpack_from('<H', memory, linear)[0]

    def field(pointer, offset, size):
        segment = word(ds * 16 + pointer)
        address = segment * 16 + offset
        if not segment or address + size > 0xa0000:
            raise ValueError('Study response field outside conventional RAM')
        return address, word(address) if size == 2 else memory[address]

    for name, ptr, offset, size, expected_address in (
            ('previous_prompt', 0x967a, 0x7906, 2, PREVIOUS_PROMPT),
            ('reminder', 0x967c, 0x7908, 1, REMINDER),
            ('matched', 0x9662, 0x7909, 1, MATCHED)):
        address, value = field(ptr, offset, size)
        state[name] = value
        state[name + '_address'] = address
        if address != expected_address:
            raise ValueError('Study response watch addresses differ from native DS pointers')

    table_segment = word(ds * 16 + 0x9652)
    action_address = table_segment * 16 + 0x02f8  # :02a8 + 4*20
    prompt_address = table_segment * 16 + 0x03f2  # :03a2 + 4*20
    if not table_segment or max(action_address + 4, prompt_address + 4) > 0xa0000:
        raise ValueError('Study terminal table outside guest RAM')
    state['terminal_action'] = memory[action_address]
    state['terminal_object'] = word(action_address + 2)
    prompt_offset, prompt_segment = word(prompt_address), word(prompt_address + 2)
    text_address = prompt_segment * 16 + prompt_offset
    if not prompt_segment or text_address + 32 > 0xa0000:
        raise ValueError('Study terminal prompt outside conventional RAM')
    end = memory.find(b'\0', text_address, text_address + 32)
    if end < 0:
        raise ValueError('Study terminal prompt is not NUL-terminated')
    state['terminal_prompt'] = memory[text_address:end].decode('cp437')
    return state


def _assert_common(lines, state):
    if any(line.startswith('research-poke') for line in lines):
        raise ValueError('native Study response replay must not poke guest RAM')
    if (state.get('study_counter_address') != COUNTER or
            any(state.get(key + '_address') != address for key, address in
                (('previous_prompt', PREVIOUS_PROMPT), ('reminder', REMINDER),
                 ('matched', MATCHED))) or
            (state.get('terminal_action'), state.get('terminal_object'),
             state.get('terminal_prompt')) != (0x84, 0, 'sleep')):
        raise ValueError('native Study state addresses or SLEEP table tail differ')


def _changes(lines):
    result = []
    for line in lines:
        if not line.startswith('byte-write '):
            continue
        info = fields(line)
        ms = int(info['ms'])
        address = int(info['address'], 16)
        if not BASE <= address <= MATCHED or ms < 70000:
            continue
        if info['cs'] != '4990' or int(info['size'], 16) not in (1, 2):
            raise ValueError('watched Study field was not written by native section 51')
        before, value = int(info['before'], 16), int(info['value'], 16)
        if before != value:
            result.append(dict(ms=ms, address=address, pc=int(info['pc'], 16),
                               old=before, new=value))
    return result


def _check_write(writes, index, address, pc, old, new, slot=None):
    if index >= len(writes):
        raise ValueError('missing original Study response write')
    item = writes[index]
    if (item['address'], item['pc'], item['old'], item['new']) != (address, pc, old, new):
        raise ValueError('Study response write order, address, or original PC differs')
    if slot is not None and not _in_slot(item['ms'], slot):
        raise ValueError('Study response write is outside its native command time slot')
    return item


def _entries(lines, pc):
    return [int(fields(line)['ms']) for line in lines
            if line.startswith('entry ') and ('pc=%x cs=4990 ' % pc) in line]


def verify_reminder(lines, state):
    """First repeated LOOK sets :7908; insistence returns player to Terminal."""
    _assert_common(lines, state)
    if (state.get('study_counter'), state.get('previous_prompt'),
            state.get('reminder'), state.get('matched'),
            state.get('player_location'), state.get('player_enclosing_room')) != \
            (1, 1, 1, 0, 0x2a1, 0x29b):
        raise ValueError('native repeated-LOOK refusal final state differs')
    writes = _changes(lines)
    if len(writes) != 3:
        raise ValueError('native repeated-LOOK trace has unexpected state changes')
    initial = _check_write(writes, 0, COUNTER, INIT, 0, 1, 18)
    previous = _check_write(writes, 1, PREVIOUS_PROMPT, PROMPT_SAVED, 0, 1, 18)
    reminder = _check_write(writes, 2, REMINDER, REMINDER_SET, 0, 1)
    insistence, auxiliary = _entries(lines, INSISTENCE), _entries(lines, AUXILIARY)
    if (len(insistence) != 1 or len(auxiliary) != 1 or
            not initial['ms'] <= previous['ms'] < reminder['ms'] <
            insistence[0] < auxiliary[0] or
            not _entries(lines, STUDY_RESPONSE)):
        raise ValueError('original Study insistence/auxiliary entry or sequence missing')
    return dict(prompt_ms=previous['ms'], reminder_ms=reminder['ms'],
                insistence_ms=insistence[0], auxiliary_ms=auxiliary[0])


def verify_terminal(lines, state):
    """Prove 19 native advances, terminal match without increment, and exit."""
    _assert_common(lines, state)
    if (state.get('study_counter'), state.get('previous_prompt'),
            state.get('reminder'), state.get('matched'),
            state.get('player_location'), state.get('player_enclosing_room')) != \
            (20, 0, 0, 1, 0x29b, 0x29b):
        raise ValueError('native SLEEP completion final state differs')
    writes = _changes(lines)
    index = 0
    _check_write(writes, index, COUNTER, INIT, 0, 1, 18)
    index += 1
    _check_write(writes, index, PREVIOUS_PROMPT, PROMPT_SAVED, 0, 1, 18)
    index += 1
    stages = []
    for stage in range(1, 20):
        origin = (POSTURE_MATCH if stage in (3, 13) else
                  ALTERNATE_MATCH if stage == 9 else PRIMARY_MATCH)
        match = _check_write(writes, index, MATCHED, origin, 0, 1, 18 + stage)
        index += 1
        advance = _check_write(writes, index, COUNTER, ADVANCE, stage, stage + 1,
                               18 + stage)
        index += 1
        clear = _check_write(writes, index, MATCHED, MATCH_CLEAR, 1, 0,
                             18 + stage)
        index += 1
        if not match['ms'] <= advance['ms'] <= clear['ms']:
            raise ValueError('Study therapy advanced outside the match/clear interval')
        if stage == 1:
            _check_write(writes, index, PREVIOUS_PROMPT, PROMPT_CLEAR, 1, 0)
            index += 1
        stages.append((stage, match['pc'], match['ms']))
    terminal = _check_write(writes, index, MATCHED, PRIMARY_MATCH, 0, 1, 38)
    index += 1
    if len(writes) != index:
        raise ValueError('native SLEEP replay has extra or missing Study state changes')
    response_entries = _entries(lines, STUDY_RESPONSE)
    auxiliary_entries = _entries(lines, AUXILIARY)
    if (not any(terminal['ms'] <= ms for ms in response_entries) or
            len(auxiliary_entries) != 1 or
            not terminal['ms'] < auxiliary_entries[0] or
            not _in_slot(auxiliary_entries[0], 38)):
        raise ValueError('terminal SLEEP skipped original response/completion helper')
    final_moves = [move for move in _verified_moves(lines)
                   if move['ms'] > auxiliary_entries[0]]
    if len(final_moves) != 2 or [(m['old'], m['new']) for m in final_moves] != [
            (0x2be, 0x2bc), (0x2bc, 0x29b)]:
        raise ValueError('SLEEP completion lacks two original player-location writes')
    return dict(stages=stages, terminal_ms=terminal['ms'],
                auxiliary_ms=auxiliary_entries[0], player_move_ms=final_moves[-1]['ms'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('refusal', type=Path, help='repeated LOOK native dump prefix')
    parser.add_argument('completion', type=Path, help='20-stage SLEEP native dump prefix')
    args = parser.parse_args()
    try:
        reminder = verify_reminder(args.refusal.with_suffix('.trace').read_text().splitlines(),
                                   inspect_response_snapshot(args.refusal))
        terminal = verify_terminal(
            args.completion.with_suffix('.trace').read_text().splitlines(),
            inspect_response_snapshot(args.completion))
    except (OSError, ValueError, KeyError, struct.error) as error:
        parser.exit(1, 'inspect_study_response: %s\n' % error)
    print('VERIFIED unmodified Study dialogue and terminal SLEEP:')
    print('  reminder at %d ms, stronger refusal at %d ms, return to Terminal' %
          (reminder['reminder_ms'], reminder['auxiliary_ms']))
    print('  19 matched exercises: 16 primary, 1 alternative (stage 9), '
          '2 posture (stages 3 and 13)')
    print('  stage-20 SLEEP flag at %d ms, completion at %d ms, player move at %d ms' %
          (terminal['terminal_ms'], terminal['auxiliary_ms'],
           terminal['player_move_ms']))
    print('  final native progress 20, no extra increment, unmodified guest state')


if __name__ == '__main__':
    main()
