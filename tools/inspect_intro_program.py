#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Inspect the original GATE.EXE's compiled sequence of intro text calls.

Reports source offsets, the three original placement arguments, text lengths,
and the observed native duration expression. No game text or executable bytes
are written to the report; the original DOS files stay read-only.
"""

import argparse
import json
from pathlib import Path
import struct


CAPTION_FUNCTION = 0x117e  # GATE.EXE file offset, native far function
FIRST_TEXT = 0x428f       # GATE.EXE file offset
CAPTION_COUNT = 18
SEQUENCE_BEGIN = 0x13fc
SEQUENCE_END = 0x1a09


def read_word(data, offset):
    if offset < 0 or offset + 2 > len(data):
        raise ValueError('truncated intro program')
    return struct.unpack_from('<H', data, offset)[0]


def caption_call_sites(exe):
    sites = []
    for position in range(len(exe) - 3):
        if exe[position:position + 2] != b'\x0e\xe8':
            continue
        displacement = struct.unpack_from('<h', exe, position + 2)[0]
        if position + 4 + displacement == CAPTION_FUNCTION:
            sites.append(position)
    return sites


def parse_caption_arguments(exe, call_site):
    """Decode five pushes before the verified `push CS; near call` pair.

    The pointer is pushed with DS, and each of the following three arguments
    is either `mov AX,imm16; push AX` or `push AX` repeating the previous word.
    A nearby unrelated instruction cannot be accepted as a caption call.
    """
    if (call_site < 5 or call_site + 4 > len(exe) or
            exe[call_site:call_site + 2] != b'\x0e\xe8' or
            call_site + 4 + struct.unpack_from('<h', exe, call_site + 2)[0]
            != CAPTION_FUNCTION):
        raise ValueError(f'invalid intro caption call at 0x{call_site:x}')
    candidates = []
    for start in range(max(0, call_site - 32), call_site - 4):
        if (exe[start] != 0xb8 or
                exe[start + 3:start + 5] != b'\x1e\x50'):
            continue
        pointer = read_word(exe, start + 1)
        position = start + 5
        current = pointer
        arguments = []
        while position < call_site:
            if exe[position] == 0xb8 and position + 3 < call_site:
                current = read_word(exe, position + 1)
                position += 3
            if position >= call_site or exe[position] != 0x50:
                break
            arguments.append(current)
            position += 1
        if position == call_site and len(arguments) == 3:
            candidates.append((pointer, arguments))
    if len(candidates) != 1:
        raise ValueError(f'ambiguous intro caption arguments at 0x{call_site:x}')
    pointer, (right, top, left) = candidates[0]
    if not 0 <= left < right <= 320 or not 0 <= top < 200:
        raise ValueError(f'intro caption dimensions out of range at 0x{call_site:x}')
    return dict(text_pointer=pointer, left=left, requested_top=top, right=right)


def native_duration_formula(exe):
    """Read the operands of the original length*45+2000 delay expression."""
    if (exe[0x12e9:0x12ee] != b'\x9a\xba\x2b\xa5\x02' or
            exe[0x12f1] != 0xb9 or exe[0x12f4:0x12f7] != b'\xf7\xe1\x05' or
            exe[0x12f9:0x12fe] != b'\x50\x0e\xe8\x1a\xfc'):
        raise ValueError('original intro text-duration code has changed')
    scale = read_word(exe, 0x12f2)
    minimum = read_word(exe, 0x12f7)
    if scale != 45 or minimum != 2000:
        raise ValueError('unknown intro text-duration constants')
    return scale, minimum


def inspect_animation_calls(exe):
    """Extract native three-argument picture-sequence calls in program order.

    These use the linked far target 0xf10:1466; it is *not* an RTL file
    offset. They are identified by the original 8086 call operands, rather
    than guessed from nearby picture resource reads.
    """
    rows = []
    marker = b'\x9a\x66\x14\x10\x0f'
    for call in range(SEQUENCE_BEGIN + 12, min(SEQUENCE_END, len(exe) - 4)):
        if exe[call:call + 5] != marker:
            continue
        values = []
        for at in (call - 12, call - 8, call - 4):
            if exe[at] != 0xb8 or exe[at + 3] != 0x50:
                raise ValueError(f'unsupported animation arguments at 0x{call:x}')
            values.append(read_word(exe, at + 1))
        count, next_resource, base_resource = values
        if (not 0 < count <= 256 or next_resource >> 8 != base_resource >> 8 or
                not 0x30 <= base_resource >> 8 <= 0x39):
            raise ValueError(f'invalid picture sequence at 0x{call:x}')
        rows.append(dict(call_file_offset=call, count=count,
                         base_resource=base_resource, next_resource=next_resource,
                         pic_file=300 + (base_resource >> 8) - 0x30,
                         base_entry=base_resource & 255,
                         next_entry=next_resource & 255))
    return rows


def inspect_logo_palette_calls(exe):
    """Recover the seven ordered palette fade IDs passed to exe:0x1c11."""
    rows = []
    for call in range(SEQUENCE_BEGIN + 4, min(SEQUENCE_END, len(exe) - 3)):
        if exe[call:call + 2] != b'\x0e\xe8':
            continue
        displacement = struct.unpack_from('<h', exe, call + 2)[0]
        if call + 4 + displacement != 0x1c11:
            continue
        if exe[call - 4] != 0xb8 or exe[call - 1] != 0x50:
            raise ValueError(f'unsupported logo fade arguments at 0x{call:x}')
        index = read_word(exe, call - 3)
        if not 1 <= index <= 7:
            raise ValueError(f'invalid logo fade index at 0x{call:x}')
        rows.append(dict(call_file_offset=call, bar=index,
                         dac_palette_index=45 + 3 * index))
    return rows


def inspect_intro_program(exe):
    if exe[:2] != b'MZ' or len(exe) <= FIRST_TEXT:
        raise ValueError('expected original GATE.EXE MZ layout')
    scale, minimum = native_duration_formula(exe)
    sites = caption_call_sites(exe)
    if len(sites) != CAPTION_COUNT:
        raise ValueError(f'expected {CAPTION_COUNT} intro calls, got {len(sites)}')
    captions = []
    text_offset = FIRST_TEXT
    pointer_delta = None
    for page, call_site in enumerate(sites):
        args = parse_caption_arguments(exe, call_site)
        try:
            end = exe.index(0, text_offset)
        except ValueError as exc:
            raise ValueError('unterminated original intro text') from exc
        if pointer_delta is None:
            pointer_delta = text_offset - args['text_pointer']
        if args['text_pointer'] + pointer_delta != text_offset:
            raise ValueError(f'non-sequential text pointer at page {page}')
        length = end - text_offset
        captions.append(dict(page=page, call_file_offset=call_site,
                             text_file_offset=text_offset, text_length=length,
                             display_ms=(minimum + scale * length) & 0xffff,
                             **args))
        text_offset = end + 1
    return dict(show_caption_file_offset=CAPTION_FUNCTION,
                strlen_file='GATE.RTL', strlen_offset=0x8298,
                delay_file_offset=0x0f18, delay_multiplier=scale,
                delay_base_ms=minimum, captions=captions,
                animations=inspect_animation_calls(exe),
                logo_palette_steps=inspect_logo_palette_calls(exe))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path, help='original GATE.EXE')
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    try:
        report = inspect_intro_program(args.exe.read_bytes())
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print('GATE.EXE compiled intro: 18 text calls through file offset 0x117e')
        print('Original text timeout: 2000ms + 45ms * strlen(text)')
        print('page call    text    chars  requested(left,top,right) timeout')
        for row in report['captions']:
            print(f'{row["page"]:>2}   0x{row["call_file_offset"]:04x}  '
                  f'0x{row["text_file_offset"]:04x} {row["text_length"]:>5} '
                  f'({row["left"]:>3},{row["requested_top"]:>3},'
                  f'{row["right"]:>3}) {row["display_ms"]:>5}ms')
        print('Picture animations (original linked call target f10:1466):')
        for row in report['animations']:
            print(f'0x{row["call_file_offset"]:04x} GATE_{row["pic_file"]:03d}.PIC '
                  f'base #{row["base_entry"]}, next #{row["next_entry"]}, '
                  f'count {row["count"]}')
        print('Logo palette reveals (original exe:0x1c11):')
        print(', '.join(f'bar {row["bar"]} -> DAC {row["dac_palette_index"]}'
                        for row in report['logo_palette_steps']))


if __name__ == '__main__':
    main()
