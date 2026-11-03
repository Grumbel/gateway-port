#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Asset-independent mutation tests for the original resident far-call verifier."""

from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from inspect_room_transfer import (COUCH_CONTINUATION, verify_presentation_wait,
                                   verify_ordinary_idle_frame, verify_transfer)


def entry(ms, pc, *, cs=0x148e, sp=0xff0, bp=0xffe, ax=0,
          stack='0,0,0', frames=''):
    return ('entry ms=%d pc=%x cs=%x sp=%x bp=%x ax=%x stack=%s frames=%s' %
            (ms, pc, cs, sp, bp, ax, stack, frames))


def trace(*, finished=True):
    # Enter with a FAR CALL from OVL 4990:2a5b, [SP+4]=destination 0x29b.
    # The EXE prologue changes BP from caller's 0x1100 to 0x0ffe.
    lines = [
        'object-watch object=d3 type=7',
        entry(81201, 0x14b64, sp=0x1000, bp=0x1100,
              stack='2a60,4990,29b,0,'),
        entry(81201, 0x14c22),
        entry(81201, 0x14c2d),
        entry(81201, 0x14cf1),
        entry(81201, 0x14d1d),
        'object-location-set ms=81201 object=d3 location=29b changed=1 return=4319:16e7',
        'object-write ms=81201 object=d3 pc=11c32 offset=c size=2 '
        'before=bc02 value=9b02',
        entry(81201, 0x14d30),
        entry(81203, 0x14d8f),
        entry(81203, 0x14dab),
    ]
    if finished:
        lines += [
            entry(84196, 0x14ed2, ax=1,
                  frames='ffe:1100:2a60:4990,1100:0:abcd:ef00'),
            entry(84196, COUCH_CONTINUATION, cs=0x4990),
        ]
    return lines


def verify(lines, finished=True):
    return verify_transfer(lines, return_cs=0x4990, return_ip=0x2a60,
                           destination=0x29b, old_location=0x2bc,
                           expect_text_skipped=True,
                           continuation=COUCH_CONTINUATION, completed=finished)


def presentation_lines():
    lines = trace()
    index = next(i for i, line in enumerate(lines) if 'pc=14dab' in line)
    lines[index + 1:index + 1] = [
        entry(81203, 0x14db2),
        entry(82285, 0x14db7),
        entry(82285, 0x14dc7),
        entry(84194, 0x14dcf),
    ]
    return lines


def idle_frame_fixture():
    lines = [entry(18176, 0x14b64, sp=0xdb9c, bp=0xdbb6,
                   stack='65a,148e,f1', frames='')]
    lines[0] += ' ss=5b46'
    ram = bytearray(0x69100)
    struct.pack_into('<4H', ram, 0x68ffa, 0xdbb6, 0x65a, 0x148e, 0xf1)
    return lines, ram, dict(SS='5b46')


class RoomTransferTests(unittest.TestCase):
    def test_synchronous_far_frame_and_pending_native_call(self):
        self.assertEqual(verify(trace())['return_ms'], 84196)
        self.assertEqual(verify(trace(finished=False), False)['commit_ms'], 81201)
        self.assertIsNone(verify(trace(finished=False), False)['return_ms'])

    def test_wrong_call_argument_or_return_ip_is_not_a_transaction(self):
        for changed in ('2a60,4990,2bc', '2a61,4990,29b', '2a60,4991,29b'):
            lines = [line.replace('2a60,4990,29b', changed) for line in trace()]
            with self.subTest(changed=changed), self.assertRaisesRegex(ValueError,
                                                                       'expected one room-transfer'):
                verify(lines)

    def test_stage_order_veto_and_native_move_are_enforced(self):
        lines = [line for line in trace() if 'pc=14cf1' not in line]
        with self.assertRaisesRegex(ValueError, 'missing/duplicate'):
            verify(lines)
        lines = trace()
        lines[3], lines[4] = lines[4], lines[3]
        with self.assertRaisesRegex(ValueError, 'out of order'):
            verify(lines)
        lines = [line for line in trace() if 'before=bc02' not in line]
        with self.assertRaisesRegex(ValueError, 'player-room move'):
            verify(lines)
        lines = [line for line in trace() if 'pc=14dab' not in line]
        with self.assertRaisesRegex(ValueError, 'text bypass'):
            verify(lines)

    def test_far_return_stack_and_continuation_are_checked(self):
        lines = [line.replace('ffe:1100:2a60:4990', 'ffe:1100:2a60:4991')
                 for line in trace()]
        with self.assertRaisesRegex(ValueError, 'frame changed'):
            verify(lines)
        lines = [line.replace('pc=14ed2', 'pc=14ed3') for line in trace()]
        with self.assertRaisesRegex(ValueError, 'never reached'):
            verify(lines)
        lines = [line for line in trace() if 'pc=4c36b' not in line]
        with self.assertRaisesRegex(ValueError, 'immediately continue'):
            verify(lines)
        lines = trace()
        lines[-2] = lines[-2].replace('ax=1', 'ax=0')
        with self.assertRaisesRegex(ValueError, 'not AX=1'):
            verify(lines)
        with self.assertRaisesRegex(ValueError, 'unexpectedly finished'):
            verify(trace(), False)
        with self.assertRaisesRegex(ValueError, 'unmodified'):
            verify(trace() + ['research-poke16 ms=81202'])

    def test_idle_presentation_waits_are_two_ordered_native_calls(self):
        wait = verify_presentation_wait(presentation_lines())
        self.assertEqual((wait['first_wait_ms'], wait['second_wait_ms']), (1082, 1909))
        for pc in ('14db2', '14db7', '14dc7', '14dcf'):
            lines = [line for line in presentation_lines() if 'pc=' + pc not in line]
            with self.subTest(pc=pc), self.assertRaisesRegex(ValueError,
                                                             'missing/duplicate'):
                verify_presentation_wait(lines)
        lines = [line.replace('ms=82285 pc=14db7', 'ms=81203 pc=14db7')
                 for line in presentation_lines()]
        with self.assertRaisesRegex(ValueError, 'positive waits'):
            verify_presentation_wait(lines)
        lines = [line.replace('ms=84196 pc=4c36b', 'ms=84199 pc=4c36b')
                 for line in presentation_lines()]
        with self.assertRaisesRegex(ValueError, 'immediately'):
            verify_presentation_wait(lines)

    def test_ordinary_no_input_keeps_original_far_return_frame_in_ram(self):
        lines, memory, regs = idle_frame_fixture()
        self.assertEqual(verify_ordinary_idle_frame(lines, memory, regs)
                         ['saved_frame_physical'], 0x68ffa)
        struct.pack_into('<H', memory, 0x68ffc, 0x9999)
        with self.assertRaisesRegex(ValueError, 'lost the original'):
            verify_ordinary_idle_frame(lines, memory, regs)
        lines, memory, regs = idle_frame_fixture()
        lines.append(entry(19000, 0x14ed2, sp=0, bp=0xdb9a, ax=1))
        with self.assertRaisesRegex(ValueError, 'unexpectedly returned'):
            verify_ordinary_idle_frame(lines, memory, regs)
        lines, memory, regs = idle_frame_fixture()
        with self.assertRaisesRegex(ValueError, 'stack segment differs'):
            verify_ordinary_idle_frame(lines, memory, dict(SS='1234'))


if __name__ == '__main__':
    unittest.main()
