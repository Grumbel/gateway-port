#!/usr/bin/env python3
"""Annotate a bounded 16-bit Gateway overlay disassembly with story text IDs.

The on-disk image mixes code, tables and relocation operands. This is a
research view, not a recursive disassembler or a C decompiler.
"""
import argparse
import pathlib
import re
import subprocess


INSTRUCTION = re.compile(r'^\s*([0-9a-f]+):\s+(?:(?:[0-9a-f]{2})\s+)+\s*(.*?)\s*$')
MOV_AX = re.compile(r'^mov\s+ax,0x([0-9a-f]+)$')
MOV_DX = re.compile(r'^mov\s+dx,0x([0-9a-f]+)$')
PUSH_DX = re.compile(r'^push\s+dx$')
PUSH_AX = re.compile(r'^push\s+ax$')
CALL_FAR = re.compile(r'^call\s+0x[0-9a-f]+:0x[0-9a-f]+$')


def text_id(ax, dx):
    """Return (segment, string) only for the validated f000:offset form."""
    if dx != 0xf000:
        return None
    return ax >> 10, ax & 0x3ff


def annotate(lines):
    """Identify contiguous immediate-load/push/call text references.

    Do not attribute a token to a call when a branch, register write, or other
    instruction intervenes. Some matches can be non-text far-pointer calls;
    callers must confirm the call target against the known formatter.
    """
    instructions = []
    for index, line in enumerate(lines):
        match = INSTRUCTION.match(line)
        if match:
            instructions.append((index, match.group(2)))
    labels = {}
    for i in range(len(instructions) - 4):
        window = instructions[i:i + 5]
        ax = MOV_AX.fullmatch(window[0][1])
        dx = MOV_DX.fullmatch(window[1][1])
        if not (ax and dx and PUSH_DX.fullmatch(window[2][1])
                and PUSH_AX.fullmatch(window[3][1]) and CALL_FAR.fullmatch(window[4][1])):
            continue
        token = text_id(int(ax.group(1), 16), int(dx.group(1), 16))
        if token is not None:
            labels[window[4][0]] = f'  ; possible story text segment={token[0]} string={token[1]}'
    return [line + labels.get(index, '') for index, line in enumerate(lines)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('overlay', type=pathlib.Path)
    parser.add_argument('--start', required=True, type=lambda s: int(s, 0), help='file offset')
    parser.add_argument('--size', type=lambda s: int(s, 0), default=0x100, help='byte count')
    args = parser.parse_args()
    if args.start < 0 or args.size <= 0 or args.start + args.size > args.overlay.stat().st_size:
        parser.error('requested range is outside the overlay')
    command = ['objdump', '-D', '-b', 'binary', '-m', 'i8086', '-M', 'intel',
               f'--start-address={args.start}', f'--stop-address={args.start + args.size}',
               str(args.overlay)]
    result = subprocess.run(command, capture_output=True, text=True, check=True)
    print('\n'.join(annotate(result.stdout.splitlines())))
    print('\nNote: linear sweep can decode embedded data as instructions; far-call targets'
          ' and relocation must be confirmed in a live overlay before translating to C.')


if __name__ == '__main__':
    main()
