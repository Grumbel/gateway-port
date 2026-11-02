#!/usr/bin/env python3
"""Verify resident 048e:0284's far-call frame and ordered room transaction.

This is a read-only check of *unmodified* Unicorn execution. Original
GATEMAIN.EXE file offset 0x5d5d maps to live 148e:0284 in the observed
default load. The caller's far-return IP:CS and destination argument are
read from the guest entry stack, not inferred from an action name.
"""

import argparse
from pathlib import Path
import struct

from inspect_study_up import _verified_moves
from summarize_navigation_trace import fields


RESIDENT_CS = 0x148e
ROOM_TRANSFER = 0x14b64       # EXE 0x5d5d, 048e:0284
PENDING = 0x14c22             # EXE 0x5e1b
EXIT_EVENT = 0x14c2d          # EXE 0x5e26, call event 0x0f
COMMIT = 0x14cf1              # EXE 0x5eea
PLAYER_MOVE = 0x14d1d         # EXE 0x5f16, 1c24:13d5
ENTER_EVENT = 0x14d30         # EXE 0x5f29, event 0x0d
TEXT_GUARD = 0x14d8f          # EXE 0x5f88, DS:3942
SKIP_ROOM_TEXT = 0x14dab      # EXE 0x5fa4, taken when DS:3942 != 0
ALTERNATE_ENTRY_CALL = 0x14db2  # EXE 0x5fab, 1c24:12db
ALTERNATE_ENTRY_RETURN = 0x14db7  # EXE 0x5fb0, DS:3943 guard
PRESENTATION_CALL = 0x14dc7  # EXE 0x5fc0, 05bd:000a
PRESENTATION_RETURN = 0x14dcf  # EXE 0x5fc8, event 0x0e setup
EPILOGUE = 0x14ed2           # EXE 0x60cb, mov SP,BP; pop BP; retf
COUCH_CONTINUATION = 0x4c36b  # OVL 0x6d73b, after caller's far CALL returns


def _native_entries(lines, pc, cs=RESIDENT_CS):
    return [(index, fields(line)) for index, line in enumerate(lines)
            if line.startswith('entry ') and
            (info := fields(line)).get('pc') == '%x' % pc and
            info.get('cs') == '%x' % cs]


def _stack_words(info):
    try:
        return [int(value, 16) for value in info['stack'].split(',') if value]
    except (KeyError, ValueError) as error:
        raise ValueError('resident entry lacks a hexadecimal far-call stack') from error


def verify_transfer(lines, *, return_cs, return_ip, destination, old_location,
                    expect_text_skipped=False, continuation=None, completed=True):
    """Check one transaction selected by its exact original return frame.

    A pending transfer has committed/moved, but has neither reached the
    resident far return nor the caller continuation within this replay.
    """
    if any(line.startswith('research-poke') for line in lines):
        raise ValueError('room-transfer evidence must be unmodified native execution')
    selected = []
    for index, info in _native_entries(lines, ROOM_TRANSFER):
        stack = _stack_words(info)
        if len(stack) >= 3 and stack[:3] == [return_ip, return_cs, destination]:
            selected.append((index, info))
    if len(selected) != 1:
        raise ValueError('expected one room-transfer far call %04x:%04x(%03x), got %d' %
                         (return_cs, return_ip, destination, len(selected)))
    start, entry = selected[0]
    next_calls = [index for index, _ in _native_entries(lines, ROOM_TRANSFER)
                  if index > start]
    limit = min(next_calls, default=len(lines))
    expected_bp = (int(entry['sp'], 16) - 2) & 0xffff
    expected_parent_bp = int(entry['bp'], 16)

    stages = (PENDING, EXIT_EVENT, COMMIT, PLAYER_MOVE, ENTER_EVENT, TEXT_GUARD)
    positions = []
    for pc in stages:
        matches = [(index, info) for index, info in _native_entries(lines, pc)
                   if start < index < limit and int(info['bp'], 16) == expected_bp]
        if len(matches) != 1:
            raise ValueError('resident room transfer missing/duplicate stage EXE PC %x' % pc)
        positions.append(matches[0][0])
    if positions != sorted(positions):
        raise ValueError('resident room-transfer stages executed out of order')

    if expect_text_skipped and not any(
            positions[-1] < index < limit and int(info['bp'], 16) == expected_bp
            for index, info in _native_entries(lines, SKIP_ROOM_TEXT)):
        raise ValueError('expected DS:3942 room-text bypass not observed')

    moves = _verified_moves(lines)
    # Both records are in the same guest millisecond in these traces. The
    # verified location setter emits its event after the PLAYER_MOVE hook;
    # check the trace-line window as well as the exact before/after words.
    room_move = [move for move in moves if move['old'] == old_location and
                 move['new'] == destination and
                 int(entry['ms']) <= move['ms'] <= int(fields(lines[positions[4]])['ms'])]
    if len(room_move) != 1:
        raise ValueError('resident transaction lacks the verified player-room move')
    location_lines = [i for i, line in enumerate(lines)
                      if line.startswith('object-location-set ') and
                      'object=d3 location=%x ' % destination in line]
    if not any(positions[3] < index < positions[4] for index in location_lines):
        raise ValueError('player-room setter did not execute inside native move call')

    returns = [(index, info) for index, info in _native_entries(lines, EPILOGUE)
               if positions[-1] < index < limit and
               int(info['bp'], 16) == expected_bp]
    if completed:
        if len(returns) != 1:
            raise ValueError('resident transfer never reached its far-return epilogue')
        end, returned = returns[0]
        parent_frame = '%x:%x:%x:%x' % (
            expected_bp, expected_parent_bp, return_ip, return_cs)
        if returned.get('frames', '').split(',')[0] != parent_frame:
            raise ValueError('room-transfer far-return frame changed across nested events')
        if int(returned['ax'], 16) != 1:
            raise ValueError('successful room-transfer far return is not AX=1')
        if continuation is not None:
            following = [(index, info) for index, info in
                         _native_entries(lines, continuation, return_cs) if index > end]
            if len(following) != 1 or (
                    int(following[0][1]['ms']) - int(returned['ms']) > 2):
                raise ValueError('caller did not immediately continue after resident RETF')
    elif returns or (continuation is not None and any(
            index > positions[-1] for index, _ in
            _native_entries(lines, continuation, return_cs))):
        raise ValueError('pending transfer unexpectedly finished in this replay')

    return dict(destination=destination, entry_ms=int(entry['ms']),
                commit_ms=int(fields(lines[positions[2]])['ms']),
                return_ms=int(returns[0][1]['ms']) if returns else None)


def verify_presentation_wait(lines):
    """Check the two timed calls on a *separately captured* idle VR replay.

    The native trace establishes PC order, nested frame and elapsed time.
    Its *input-free* nature is established by the caller's replay schedule,
    not inferred from an absence of keystroke lines in this trace format.
    """
    if any(line.startswith('research-poke') for line in lines):
        raise ValueError('presentation wait requires unmodified native execution')
    selected = [(index, info) for index, info in _native_entries(lines, ROOM_TRANSFER)
                if _stack_words(info)[:3] == [0x2a60, 0x4990, 0x29b]]
    if len(selected) != 1:
        raise ValueError('VR presentation wait lacks unique 4990:2a60 far call')
    start, entry = selected[0]
    bp = (int(entry['sp'], 16) - 2) & 0xffff
    stages = (SKIP_ROOM_TEXT, ALTERNATE_ENTRY_CALL, ALTERNATE_ENTRY_RETURN,
              PRESENTATION_CALL, PRESENTATION_RETURN, EPILOGUE)
    seen = []
    for pc in stages:
        matches = [(index, info) for index, info in _native_entries(lines, pc)
                   if index > start and int(info['bp'], 16) == bp]
        if len(matches) != 1:
            raise ValueError('presentation wait missing/duplicate EXE PC %x' % pc)
        seen.append(matches[0])
    positions = [index for index, _ in seen]
    if positions != sorted(positions):
        raise ValueError('presentation calls or far return out of native order')
    milliseconds = [int(info['ms']) for _, info in seen]
    first = milliseconds[2] - milliseconds[1]
    second = milliseconds[4] - milliseconds[3]
    if first <= 0 or second <= 0 or milliseconds != sorted(milliseconds):
        raise ValueError('native presentation calls lack measured positive waits')
    couch = [(index, info) for index, info in
             _native_entries(lines, COUCH_CONTINUATION, 0x4990)
             if index > positions[-1]]
    if len(couch) != 1 or int(couch[0][1]['ms']) != milliseconds[-1]:
        raise ValueError('native VR caller did not continue immediately after presentation')
    return dict(first_wait_ms=first, second_wait_ms=second,
                resident_return_ms=milliseconds[-1])


def verify_ordinary_idle_frame(lines, memory, registers):
    """Read the still-live *original* 048e:0284 frame from the guest RAM dump.

    Used only with the independently scheduled EAST + long idle replay;
    no second user key is queued in that scenario. This validates that the
    call has not returned, even when no later code-hook event is emitted.
    """
    if any(line.startswith('research-poke') for line in lines):
        raise ValueError('ordinary idle control requires unmodified guest state')
    selected = [(index, info) for index, info in _native_entries(lines, ROOM_TRANSFER)
                if _stack_words(info)[:3] == [0x65a, 0x148e, 0xf1]]
    if len(selected) != 1:
        raise ValueError('idle control lacks unique ordinary EAST far-call stack')
    start, entry = selected[0]
    frame_bp = (int(entry['sp'], 16) - 2) & 0xffff
    if any(index > start and int(info['bp'], 16) == frame_bp
           for index, info in _native_entries(lines, EPILOGUE)):
        raise ValueError('ordinary room-transfer frame unexpectedly returned')
    ss = int(entry['ss'], 16)
    if int(registers.get('SS', '-1'), 16) != ss:
        raise ValueError('snapshot stack segment differs from resident entry')
    physical = ss * 16 + frame_bp
    if physical + 8 > len(memory):
        raise ValueError('resident saved frame is outside native memory snapshot')
    words = struct.unpack_from('<4H', memory, physical)
    expected = (int(entry['bp'], 16), 0x65a, 0x148e, 0xf1)
    if words != expected:
        raise ValueError('native idle snapshot lost the original far-return frame')
    return dict(saved_frame_physical=physical, frame_words=words)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path, help='original native.trace with resident hooks')
    parser.add_argument('--idle-presentation-trace', type=Path,
                        help='separate no-follow-up-input VR replay with detailed '
                             'presentation CALL/return instruction hooks')
    parser.add_argument('--ordinary-idle-prefix', type=Path,
                        help='EAST-only native replay prefix (.trace/.bin/.regs) '
                             'with extended idle time and no second key')
    args = parser.parse_args()
    try:
        lines = args.trace.read_text().splitlines()
        ordinary = verify_transfer(lines, return_cs=0x148e, return_ip=0x65a,
                                   destination=0xf1, old_location=0xee)
        vr = verify_transfer(lines, return_cs=0x4990, return_ip=0x2a60,
                             destination=0x29b, old_location=0x2bc,
                             expect_text_skipped=True,
                             continuation=COUCH_CONTINUATION)
        wait = (verify_presentation_wait(
            args.idle_presentation_trace.read_text().splitlines())
            if args.idle_presentation_trace else None)
        idle = None
        if args.ordinary_idle_prefix:
            prefix = args.ordinary_idle_prefix
            registers = dict(line.split('=', 1) for line in
                             prefix.with_suffix('.regs').read_text().splitlines()
                             if '=' in line)
            idle = verify_ordinary_idle_frame(
                prefix.with_suffix('.trace').read_text().splitlines(),
                prefix.with_suffix('.bin').read_bytes(), registers)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, 'inspect_room_transfer: %s\n' % error)
    print('VERIFIED original resident room-transfer ABI and native return:')
    print('  Ordinary exit: 0x0ee -> 0x0f1, entry %d ms, return %d ms' %
          (ordinary['entry_ms'], ordinary['return_ms']))
    print('  VR return: Study -> Terminal, entry %d ms, return %d ms' %
          (vr['entry_ms'], vr['return_ms']))
    print('  VR couch continuation follows the resident RETF, with no synthetic unwind')
    if wait:
        print('  No-input VR presentation: alternate entry %d ms, room presentation %d ms; '
              'RETF at %d ms' % (wait['first_wait_ms'], wait['second_wait_ms'],
                                 wait['resident_return_ms']))
    if idle:
        print('  No-input EAST: original room-transfer far-return frame remains in '
              'native RAM at 0x%x' % idle['saved_frame_physical'])


if __name__ == '__main__':
    main()
