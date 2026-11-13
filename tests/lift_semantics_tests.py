#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Lift hand-written snippets and compare them with Unicorn on random states.

Usage: lift_semantics_tests.py LIFT-CHECK [--keep DIR]
Needs nasm and a C compiler; exits 77 (skip) without them.
"""
import argparse
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import disassemble_gateway as D  # noqa: E402
import lift_x86 as L  # noqa: E402

ALU = ['add', 'adc', 'sub', 'sbb', 'cmp', 'and', 'or', 'xor', 'test']
SHIFTS = ['rol', 'ror', 'rcl', 'rcr', 'shl', 'shr', 'sar']
JCC = ['jo', 'jno', 'jb', 'jae', 'jz', 'jnz', 'jbe', 'ja', 'js', 'jns', 'jp', 'jnp',
       'jl', 'jge', 'jle', 'jg']

SNIPPETS = []
for op in ALU:
    SNIPPETS.append('%s ax, bx\n%s cl, dh\n%s dx, 0x1234\n%s word [bx+si+4], ax\n'
                    '%s byte [bp-2], 0x7f\n%s si, %s' % ((op,) * 6 + (
                        '0x8001' if op == 'test' else 'byte -3',)))
for op in ('inc', 'dec', 'neg', 'not'):
    SNIPPETS.append('%s ax\n%s byte [di+1]\n%s bl\n%s word [bx]' % ((op,) * 4))
for op in SHIFTS:
    SNIPPETS.append('%s ax, 1\n%s bl, 1\n%s word [si], 1\n%s dx, 3' % ((op,) * 4))
    SNIPPETS.append('and cl, 7\n%s ax, cl\n%s bh, cl' % ((op,) * 2))
for cond in JCC:
    SNIPPETS.append('xor dx, dx\ncmp ax, bx\n%s .t\nmov dx, 1\n.t:\nadd cx, bx\n%s .u\n'
                    'inc dx\n.u:' % (cond, cond))
SNIPPETS += [
    'mul bx', 'mul bl', 'imul cx', 'imul ch', 'imul ax, bx, 7', 'imul dx, word [si], -300',
    'or bx, 1\nxor dx, dx\ndiv bx', 'or bl, 1\nxor ah, ah\ndiv bl',
    'or bx, 1\ncwd\nidiv bx', 'cbw\ncwd', 'xchg ax, si\nxchg bl, ch',
    'lea ax, [bx+di+0x40]\nlea dx, [bp-6]', 'les di, [bx+2]\nlds si, [bp+di]',
    'push ax\npush word [bx]\npop cx\npop word [si+2]',
    'pushf\npop ax\nstc\npushf\npop bx\nclc\ncld\nstd\npushf\npop dx\ncld',
    'and bx, 0x0cd5\npush bx\npopf\nmov ax, 0',
    'cld\nand cx, 15\nrep movsb', 'cld\nand cx, 7\nrep movsw', 'std\nand cx, 7\nrep stosw\ncld',
    'cld\nlodsb\nlodsw', 'cld\nand cx, 15\nrepne scasb', 'cld\nand cx, 15\nrepe cmpsb',
    'cld\nes movsb\nstosb',
    'xlatb', 'mov ah, bl\nmov bh, al\nmov [es:di], ah\nmov cl, [ss:bx+si]',
    'and cx, 7\ninc cx\n.l:\ninc ax\nloop .l', 'jcxz .z\nmov ax, 1\n.z:',
    'and bx, 3\nadd bx, bx\njmp word [cs:bx+.tab]\n.tab: dw .a, .b, .c, .d\n'
    '.a: mov ax, 1\njmp .e\n.b: mov ax, 2\njmp .e\n.c: mov ax, 3\njmp .e\n.d: mov ax, 4\n.e:',
    'call .sub\nadd ax, dx\njmp .done\n.sub: mov dx, 5\nret\n.done:',
    'push bp\nmov bp, sp\nsub sp, 4\nmov word [bp-2], ax\nmov word [bp-4], bx\n'
    'mov ax, [bp-4]\nmov sp, bp\npop bp',
]


def build(work):
    blobs, entries = bytearray(), []
    for i, body in enumerate(SNIPPETS):
        src = work / ('s%03d.asm' % i)
        src.write_text('bits 16\ncpu 286\norg 0x%x\nentry:\n%s\nretf\n' % (len(blobs), body))
        out = work / ('s%03d.bin' % i)
        subprocess.run(['nasm', '-f', 'bin', '-o', str(out), str(src)], check=True)
        entries.append(len(blobs))
        blobs += out.read_bytes()
        blobs += b'\xcc' * ((-len(blobs)) % 16)
    return bytes(blobs), entries


class Prog:
    def __init__(self, unit):
        self.units, self.sections, self.exe, self.startup = [unit], [], None, None

    def unit_for_far(self, src, seg, off):
        return None, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('check')
    ap.add_argument('--keep')
    args = ap.parse_args()
    cc = shutil.which('cc') or shutil.which('gcc')
    if not shutil.which('nasm') or not cc:
        print('nasm or cc missing; skipping')
        return 77
    work = pathlib.Path(args.keep or tempfile.mkdtemp(prefix='lift-sem-'))
    work.mkdir(parents=True, exist_ok=True)
    code, entries = build(work)
    unit = D.Unit('ovl-999', code, 0x20000, 0, 999, None, 0x2000)
    unit.default_cs[0] = 0
    trav = D.Traversal(Prog(unit))
    for e in entries:
        if not trav.speculate(unit, e, 0, 'snippet'):
            raise SystemExit('snippet at %#x does not decode: %r' % (e, trav.rejected[-1]))
    lifted, failed = L.lift_unit(unit)
    if failed:
        raise SystemExit('not lifted: %r' % failed)
    (work / 'code.bin').write_bytes(code)
    (work / 'lifted.c').write_text(L.c_module(unit, lifted, failed))
    subprocess.run([cc, '-std=c99', '-O1', '-Wall', '-Wno-unused-label', '-Werror', '-fPIC',
                    '-shared', '-I' + str(ROOT / 'src'), '-o', str(work / 'lifted.so'),
                    str(work / 'lifted.c')], check=True)
    (work / 'entries.txt').write_text(''.join('%x\n' % e for e in entries))
    rc = subprocess.run([args.check, str(work / 'lifted.so'), str(work / 'code.bin'),
                         str(work / 'entries.txt')]).returncode
    if not args.keep:
        shutil.rmtree(work)
    return rc


if __name__ == '__main__':
    sys.exit(main())
