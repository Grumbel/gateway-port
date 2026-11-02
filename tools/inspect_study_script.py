#!/usr/bin/env python3
"""Decode the 19 native Study therapy instructions from a guest RAM snapshot.

Original GATEMAIN.OVL:0x6cf24/0x6cf7c/0x6cffa indexes two far-pointer
tables through [DS:9652]. These are read-only research records, not a story
bytecode interpreter. Each pointer is either a F000 text token or an actual
guest memory C string; the response table is used after the native action.
"""

import argparse
from pathlib import Path
import struct

from gateway_strings import GatewayStrings
from inspect_beach_pin import inspect_snapshot


MEMORY_LIMIT = 0xa0000
TABLE_POINTER_OFFSET = 0x9652
PROMPT_TABLE_OFFSET = 0x03a2
RESPONSE_TABLE_OFFSET = 0x03f2
FIRST_STAGE = 1
LAST_STAGE = 19
MAX_C_STRING = 4096


def _word(memory, linear):
    if not 0 <= linear <= MEMORY_LIMIT - 2 or linear + 2 > len(memory):
        raise ValueError('Study table word outside conventional guest memory')
    return struct.unpack_from('<H', memory, linear)[0]


def decode_study_script(memory, ds, strings):
    """Return stage, instruction and response with strict guest pointer checks.

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

    stages = []
    for stage in range(FIRST_STAGE, LAST_STAGE + 1):
        stages.append(dict(
            stage=stage,
            instruction=far_text(PROMPT_TABLE_OFFSET + 4 * stage),
            response=far_text(RESPONSE_TABLE_OFFSET + 4 * stage),
        ))
    return stages


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('snapshot', type=Path, help='native dump prefix without .bin/.regs')
    parser.add_argument('--strings', type=Path, required=True, help='original GATESTR.DAT')
    args = parser.parse_args()
    try:
        state = inspect_snapshot(args.snapshot)
        stages = decode_study_script(args.snapshot.with_suffix('.bin').read_bytes(),
                                     state['native_data_segment'],
                                     GatewayStrings(args.strings.read_bytes()))
    except (OSError, ValueError, KeyError, struct.error) as error:
        parser.exit(1, 'inspect_study_script: %s\n' % error)
    for stage in stages:
        print('%2d: %-44s | %s' % (stage['stage'],
                                    stage['instruction'], stage['response']))
    print('19 far-pointer stages from an original native snapshot; these strings '
          'do not establish that any corresponding player action succeeded.')


if __name__ == '__main__':
    main()
