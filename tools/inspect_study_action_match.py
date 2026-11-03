#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Verify the native Study action-match flag during three natural commands.

Requires the documented unmodified puzzle replay: original SCREAM, FEEL
COUCH, UP at Study progress 1, 2, 3, with byte-watch on physical 0x62d69
and hooks at the native Study room event handler and progress handler.
This demonstrates two room-event-1 matcher writes and the distinct UP
callback response path, not the acceptance of all 19 action-table entries.
"""

import argparse
from pathlib import Path

from inspect_study_up import inspect_study_snapshot
from inspect_study_therapy import _in_slot
from summarize_navigation_trace import fields


FLAG_ADDRESS = 0x62d69
ROOM_EVENT_ENTRY = 0x4a151          # OVL 0x6b521
PRIMARY_MATCH_WRITE = 0x4a370      # OVL 0x6b740
UP_RESPONSE_MATCH_WRITE = 0x4bb1c  # OVL 0x6ceec
FLAG_CLEAR = 0x4bc4c               # OVL 0x6d01c
RESPONSE_ENTRY = 0x4bab5           # OVL 0x6ce85


def verify_study_action_match(lines, state):
    """Return first three flag-write timestamps; reject altered/missing events."""
    if any(line.startswith('research-poke') for line in lines):
        raise ValueError('Study action match requires unmodified native RAM')
    if (state.get('study_counter') != 4 or
            state.get('study_counter_address') != 0x62d64 or
            state.get('player_location') != 0x2bc or
            state.get('player_enclosing_room') != 0x2bc):
        raise ValueError('native Study after UP must have counter 4 and standing player')

    writes = []
    room_events = []
    responses = []
    for line in lines:
        if not (line.startswith('entry ') or line.startswith('byte-write ')):
            continue
        record = fields(line)
        ms = int(record['ms'])
        pc = int(record['pc'], 16)
        if line.startswith('entry '):
            if (pc == ROOM_EVENT_ENTRY and record.get('cs') == '4990' and
                    record.get('stack', '').startswith('701,2c24,1,')):
                room_events.append(ms)
            if pc == RESPONSE_ENTRY and record.get('cs') == '4990':
                responses.append(ms)
            continue
        if int(record['address'], 16) != FLAG_ADDRESS or ms < 70000:
            continue
        if record['cs'] != '4990' or int(record['size'], 16) != 1:
            raise ValueError('Study flag was written outside section-51 byte-sized code')
        writes.append(dict(ms=ms, pc=pc, old=int(record['before'], 16),
                           new=int(record['value'], 16)))

    if len(writes) != 6:
        raise ValueError('expected exactly three native Study flag set/clear pairs')
    result = []
    for stage in range(1, 4):
        written, cleared = writes[2 * (stage - 1):2 * stage]
        expected_pc = PRIMARY_MATCH_WRITE if stage < 3 else UP_RESPONSE_MATCH_WRITE
        if (written['pc'] != expected_pc or written['old'] != 0 or
                written['new'] != 1 or not _in_slot(written['ms'], 18 + stage)):
            raise ValueError('Study stage %d did not set the flag via native code' % stage)
        if (cleared['pc'] != FLAG_CLEAR or cleared['old'] != 1 or
                cleared['new'] != 0 or not written['ms'] <= cleared['ms'] or
                not _in_slot(cleared['ms'], 18 + stage)):
            raise ValueError('Study stage %d did not clear flag in response' % stage)
        if not any(_in_slot(ms, 18 + stage) and ms <= written['ms']
                   for ms in room_events):
            raise ValueError('Study stage %d lacks original room event-1 entry' % stage)
        if not any(_in_slot(ms, 18 + stage) and ms <= cleared['ms']
                   for ms in responses):
            raise ValueError('Study stage %d lacks native response-handler entry' % stage)
        result.append(written['ms'])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('prefix', type=Path, help='original-game trace/dump prefix')
    args = parser.parse_args()
    try:
        result = verify_study_action_match(
            args.prefix.with_suffix('.trace').read_text().splitlines(),
            inspect_study_snapshot(args.prefix))
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, 'inspect_study_action_match: %s\n' % error)
    print('VERIFIED unmodified Study stages 1–3: flag :7909 set at %s ms' %
          ', '.join(str(ms) for ms in result))
    print('  SCREAM and FEEL COUCH: original Study room event-1 primary matcher')
    print('  UP: separate native response-handler match following exit callback')
    print('  each flag reset in response; final native progress 4, player in Study')


if __name__ == '__main__':
    main()
