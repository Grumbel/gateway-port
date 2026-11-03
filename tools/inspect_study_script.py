#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Decode the 19 Study exercises and terminal SLEEP stage from native RAM.

Original GATEMAIN.OVL:0x6cf24/0x6cf7c/0x6cffa indexes two far-pointer
tables through [DS:9652]. These are read-only research records, not a story
bytecode interpreter. Each pointer is either a F000 text token or an actual
guest memory C string; the response table is used after the native action.
The event-1 matcher at OVL:0x6b6ec/0x6b735/0x6b7dc uses two additional
tables in the same segment for action codes, object targets and alternatives.
"""

import argparse
import json
from pathlib import Path
import struct

from gateway_strings import GatewayStrings
from inspect_beach_pin import inspect_snapshot


MEMORY_LIMIT = 0xa0000
TABLE_POINTER_OFFSET = 0x9652
ACTION_TABLE_OFFSET = 0x02a8
ALTERNATIVES_OFFSET = 0x02fc
ALTERNATIVES_COUNT = 5
PROMPT_TABLE_OFFSET = 0x03a2
RESPONSE_TABLE_OFFSET = 0x03f2
FIRST_STAGE = 1
LAST_STAGE = 20
LAST_RESPONSE_STAGE = 19
MAX_C_STRING = 4096


def _word(memory, linear):
    if not 0 <= linear <= MEMORY_LIMIT - 2 or linear + 2 > len(memory):
        raise ValueError('Study table word outside conventional guest memory')
    return struct.unpack_from('<H', memory, linear)[0]


def decode_study_script(memory, ds, strings):
    """Return native stage matching data and text with strict guest pointer checks.

    `strings` can be any object exposing `get(segment_index, string_index)`;
    a synthetic provider keeps the pointer/array tests asset-independent.
    """
    if len(memory) < MEMORY_LIMIT or not 0 < ds < 0x10000:
        raise ValueError('Study requires conventional RAM and a valid native DS')
    table_segment = _word(memory, ds * 16 + TABLE_POINTER_OFFSET)
    if not table_segment:
        raise ValueError('Study script table segment is null')

    def far_text(table_offset):
        linear = table_segment * 16 + table_offset
        offset = _word(memory, linear)
        segment = _word(memory, linear + 2)
        if segment == 0xf000:
            try:
                return strings.get(offset >> 10, offset & 0x3ff)
            except (IndexError, ValueError) as error:
                raise ValueError('invalid F000 Study text token') from error
        if not segment:
            raise ValueError('null Study script string segment')
        address = segment * 16 + offset
        if not 0 <= address < MEMORY_LIMIT:
            raise ValueError('Study script string pointer outside conventional RAM')
        end = memory.find(b'\0', address, min(address + MAX_C_STRING, MEMORY_LIMIT))
        if end == -1:
            raise ValueError('unterminated native Study script string')
        return memory[address:end].decode('cp437')

    alternatives = {}
    for index in range(ALTERNATIVES_COUNT):
        linear = table_segment * 16 + ALTERNATIVES_OFFSET + 4 * index
        stage = _word(memory, linear)
        if not FIRST_STAGE <= stage <= LAST_STAGE or stage in alternatives:
            raise ValueError('invalid or duplicate native Study alternative stage')
        # Original code compares both bytes, including a zero-valued byte.
        alternatives[stage] = [_byte(memory, linear + 2),
                               _byte(memory, linear + 3)]

    stages = []
    for stage in range(FIRST_STAGE, LAST_STAGE + 1):
        linear = table_segment * 16 + ACTION_TABLE_OFFSET + 4 * stage
        stages.append(dict(
            stage=stage,
            action_code=_byte(memory, linear),
            target_object=_word(memory, linear + 2),
            alternative_codes=alternatives.get(stage, []),
            instruction=far_text(PROMPT_TABLE_OFFSET + 4 * stage),
            # Stage 20 intentionally has no per-stage response pointer:
            # the native accepted path prints F000:70b0 and calls 0x6d564.
            response=(far_text(RESPONSE_TABLE_OFFSET + 4 * stage)
                      if stage <= LAST_RESPONSE_STAGE else None),
        ))
    return stages


def _byte(memory, linear):
    if not 0 <= linear < MEMORY_LIMIT or linear >= len(memory):
        raise ValueError('Study table byte outside conventional guest memory')
    return memory[linear]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('snapshot', type=Path, help='native dump prefix without .bin/.regs')
    parser.add_argument('--strings', type=Path, required=True, help='original GATESTR.DAT')
    parser.add_argument('--format', choices=('text', 'json'), default='text',
                        help='emit a human-readable listing or structured records')
    args = parser.parse_args()
    try:
        state = inspect_snapshot(args.snapshot)
        stages = decode_study_script(args.snapshot.with_suffix('.bin').read_bytes(),
                                     state['native_data_segment'],
                                     GatewayStrings(args.strings.read_bytes()))
    except (OSError, ValueError, KeyError, struct.error) as error:
        parser.exit(1, 'inspect_study_script: %s\n' % error)
    if args.format == 'json':
        print(json.dumps(stages, indent=2, ensure_ascii=False))
    else:
        for stage in stages:
            alternatives = ','.join('%02x' % code for code in stage['alternative_codes'])
            print('%2d: action=%02x target=%04x alternatives=%-5s | %-44s | %s' % (
                stage['stage'], stage['action_code'], stage['target_object'],
                alternatives or '-', stage['instruction'], stage['response']))
        print('19 exercises plus terminal SLEEP stage; the decoded matcher entries do not establish '
              'that every corresponding player command succeeded.')


if __name__ == '__main__':
    main()
