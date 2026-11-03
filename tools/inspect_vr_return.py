#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Cross-check VR-return branches and their deferred native continuation.

Inputs are four *unmodified* native replays: Study insistence with an extra
command, Study SLEEP stopping at Terminal arrival, SLEEP plus a following LOOK,
and Beach success with the pin awarded. Original file offsets below resolve
through the verified RTLink section-51 image base 0x6acd0, live at 4990:0000.
This does not run a story interpreter or infer the meaning of result word :7920.
"""

import argparse
from pathlib import Path

from inspect_beach_pin import inspect_snapshot
from inspect_study_response import inspect_response_snapshot
from inspect_study_up import _verified_moves
from summarize_navigation_trace import fields


# file offset minus section-51 image file offset 0x6acd0, plus live 0x49900
VR_ENTRY = 0x4c194             # OVL 0x6d564
TECHNICIAN_GUARD_PASSED = 0x4c1c0  # OVL 0x6d590
NON_AWARD = 0x4c213            # OVL 0x6d5e3
NO_RESULT = 0x4c2ae           # OVL 0x6d67e
CLEANUP = 0x4c2d9             # OVL 0x6d6a9
CLEANUP_FLAG = 0x4c2e9        # OVL 0x6d6b9
TRANSFER = 0x4c357            # OVL 0x6d727
RESUME_COUCH = 0x4c36b        # OVL 0x6d73b
RETURN = 0x4c377              # OVL 0x6d747
RESULT_BYTE_ADDRESSES = {0x62d5a, 0x62d80, 0x62d81, 0x62d87}


def _entries(lines, pc):
    return [(int(info['ms']), info) for line in lines if line.startswith('entry ')
            and (info := fields(line)).get('pc') == '%x' % pc and
            info.get('cs') == '4990' and int(info['ms']) >= 70000]


def _single_entry(lines, pc):
    events = _entries(lines, pc)
    if len(events) != 1:
        raise ValueError('native VR-return instruction %x expected exactly once, got %d' %
                         (pc, len(events)))
    return events[0][0]


def _changed_after_start(lines, *, require_watches):
    for line in lines:
        if not line.startswith('byte-write '):
            continue
        info = fields(line)
        if (int(info['ms']) >= 70000 and
                int(info['address'], 16) in RESULT_BYTE_ADDRESSES and
                int(info['before'], 16) != int(info['value'], 16)):
            raise ValueError('native VR-return result guard changed during Study replay')
    # The trace must actually have watched these native physical fields.
    watches = {int(fields(line)['address'], 16) for line in lines
               if line.startswith('byte-write ')}
    if require_watches and not RESULT_BYTE_ADDRESSES.issubset(watches):
        raise ValueError('VR-return branch input bytes were not all watched')


def verify_study_return(lines, state, *, resumed, progress, require_watches=True,
                        require_cleanup_hook=True):
    if any(line.startswith('research-poke') for line in lines):
        raise ValueError('VR return requires unmodified native state')
    if (state['study_counter'] != progress or
            state['matched'] != (1 if progress == 20 else 0) or
            state['beach_success_byte_return_view'] != 0 or
            state['player_enclosing_room'] != 0x29b or
            state['result_word_7920'] != 0 or state['result_seen_7927'] != 0):
        raise ValueError('native Study return input or room snapshot differs')
    if not resumed and (state['no_result_delay_44e4'] != 0x38 or
                        state['cleanup_marker_3942'] != 1):
        raise ValueError('paused native VR return missed no-result timer/cleanup fields')
    _changed_after_start(lines, require_watches=require_watches)
    pcs = [VR_ENTRY, TECHNICIAN_GUARD_PASSED, NON_AWARD, NO_RESULT, CLEANUP]
    if require_cleanup_hook:
        pcs.append(CLEANUP_FLAG)
    pcs.append(TRANSFER)
    ordered = [_single_entry(lines, pc) for pc in pcs]
    positions = [next(i for i, line in enumerate(lines)
                      if line.startswith('entry ') and
                      (info := fields(line)).get('pc') == '%x' % pc and
                      info.get('cs') == '4990' and int(info['ms']) >= 70000)
                 for pc in pcs]
    if ordered != sorted(ordered) or positions != sorted(positions):
        raise ValueError('original VR-return branch instructions executed out of order')
    guard = _entries(lines, TECHNICIAN_GUARD_PASSED)[0][1]
    if int(guard.get('ax', '0'), 16) != 1:
        raise ValueError('native technician relation guard did not return true')
    # Native unseating and subsequent room-transfer service may occur in the
    # same millisecond as the TRANSFER instruction. Keep ordering by trace.
    around = [m for m in _verified_moves(lines)
              if ordered[0] <= m['ms'] <= ordered[-1] + 2]
    if [(m['old'], m['new']) for m in around] != [
            (0x2be, 0x2bc), (0x2bc, 0x29b)]:
        raise ValueError('native VR return lacked Study couch -> room -> Terminal moves')
    if not any(line.startswith('object-location-set ') and
               'object=e4 location=0' in line and
               ordered[0] <= int(fields(line)['ms']) <= ordered[-1] + 2
               for line in lines):
        raise ValueError('native VR return missed temporary object cleanup')
    if not any(line.startswith('object-flag-set ') and
               'object=165 property=b' in line and
               ordered[0] <= int(fields(line)['ms']) <= ordered[-1] + 2
               for line in lines):
        raise ValueError('native VR return missed shared wall-property setter')

    continuation = _entries(lines, RESUME_COUCH)
    retf = _entries(lines, RETURN)
    if resumed:
        if len(continuation) != 1 or len(retf) != 1 or not (
                ordered[-1] < continuation[0][0] <= retf[0][0]):
            raise ValueError('native VR return did not resume after room transfer')
        if not any(m['old'] == 0x29b and m['new'] == 0x2a1 and
                   continuation[0][0] <= m['ms'] <= retf[0][0] + 2
                   for m in _verified_moves(lines)):
            raise ValueError('resumed native return missed Terminal couch move')
        if state['player_location'] != 0x2a1:
            raise ValueError('resumed VR return player is not on Terminal couch')
    else:
        if continuation or retf or state['player_location'] != 0x29b:
            raise ValueError('paused VR return unexpectedly executed deferred couch move')
    return dict(entry_ms=ordered[0], transfer_ms=ordered[-1],
                resume_ms=continuation[0][0] if continuation else None)


def verify_beach_reward(lines, state):
    if any(line.startswith('research-poke') for line in lines):
        raise ValueError('Beach reward requires unmodified native input')
    if (state['beach_success_byte_return_view'] != 1 or
            state['pin_location'] not in (0x29b, 0x0d3)):
        raise ValueError('native Beach result or pin location differs')
    stages = (
        ('Beach success flag', 'byte-write ', ('address=62d5a', 'pc=4a908',
                                            'before=0 value=1')),
        ('VR return invoked', 'entry ', ('pc=4c194', 'cs=4990')),
        ('pin moved to Terminal', 'object-location-set ',
         ('object=2a9', 'location=29b', 'changed=1')),
        ('pin visibility cleared', 'object-flag-clear ',
         ('object=2a9', 'property=8', 'changed=1')),
    )
    index, times = 0, []
    for name, prefix, terms in stages:
        while index < len(lines) and not (
                lines[index].startswith(prefix) and all(term in lines[index] for term in terms)):
            index += 1
        if index == len(lines):
            raise ValueError('missing ordered native Beach reward: ' + name)
        times.append(int(fields(lines[index])['ms']))
        index += 1
    if times != sorted(times):
        raise ValueError('Beach reward observations out of order')
    return dict(success_ms=times[0], helper_ms=times[1], pin_ms=times[2])


def return_fields(prefix):
    state = inspect_response_snapshot(prefix)
    memory = prefix.with_suffix('.bin').read_bytes()
    ds = state['native_data_segment']
    def word(address):
        return int.from_bytes(memory[address:address + 2], 'little')
    for name, ptr, offset, size in (
            ('result_word_7920', 0x9668, 0x7920, 2),
            ('result_seen_7927', 0x966a, 0x7927, 1),
            ('no_result_delay_44e4', 0x96a0, 0x44e4, 2),
            ('cleanup_marker_3942', 0x966e, 0x3942, 1)):
        segment = word(ds * 16 + ptr)
        address = segment * 16 + offset
        if not segment or address + size > 0xa0000 or address != ds * 16 + offset:
            raise ValueError('VR result pointer differs from known native guest DS')
        state[name] = word(address) if size == 2 else memory[address]
    return state


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('refusal', type=Path)
    parser.add_argument('sleep_paused', type=Path)
    parser.add_argument('sleep_resumed', type=Path)
    parser.add_argument('beach_reward', type=Path)
    args = parser.parse_args()
    try:
        study = []
        for prefix, resumed, progress, watched, cleanup_hook in (
                (args.refusal, True, 1, True, True),
                (args.sleep_paused, False, 20, True, True),
                (args.sleep_resumed, True, 20, False, False)):
            study.append(verify_study_return(
                prefix.with_suffix('.trace').read_text().splitlines(),
                return_fields(prefix), resumed=resumed, progress=progress,
                require_watches=watched, require_cleanup_hook=cleanup_hook))
        beach = verify_beach_reward(
            args.beach_reward.with_suffix('.trace').read_text().splitlines(),
            inspect_snapshot(args.beach_reward))
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, 'inspect_vr_return: %s\n' % error)
    print('VERIFIED original VR-return native dispatcher:')
    print('  Study refusal: transferred at %d ms, resumed at %d ms' %
          (study[0]['transfer_ms'], study[0]['resume_ms']))
    print('  SLEEP: transferred at %d ms; paused snapshot has no couch move' %
          study[1]['transfer_ms'])
    print('  SLEEP after extra LOOK: resumed at %d ms and moved to Terminal couch' %
          study[2]['resume_ms'])
    print('  Beach: success at %d ms, pin moved at %d ms' %
          (beach['success_ms'], beach['pin_ms']))


if __name__ == '__main__':
    main()
