#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Instruction-level x86 -> C lifter for decoded Gateway functions.

Each function found by ``tools/disassemble_gateway.py`` becomes one C
function against ``src/lift_abi.h``. Registers and flags live in C locals;
memory is guest memory. Every call, return, far jump and jump out of the
function body ends the C function with CS:IP set to the next guest
instruction, and the instruction after each call is a resume point, so the
runtime can hand control back and forth with the emulator. Instructions the
lifter does not translate (I/O, interrupts, x87, BCD...) are left to the
emulator: the C function stops before them with ``GW_LIFT_EMULATE`` and the
following instruction becomes a resume point.

The output is a mechanical translation of original program code and belongs
under ``build/`` only. See docs/lifter.md. Example::

    python3 tools/lift_x86.py ../gateway-dos/GATEMAIN.EXE ../gateway-dos/GATEMAIN.OVL \\
        --rtl ../gateway-dos/GATE.RTL --manifest build/handler-coverage/rtlink-manifest.json \\
        --unit ovl-038 --output build/lift/ovl-038.c
"""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import disassemble_gateway as D  # noqa: E402
import x86_16 as X  # noqa: E402

R16 = ('ax', 'cx', 'dx', 'bx', 'sp', 'bp', 'si', 'di')
SREG = ('es', 'cs', 'ss', 'ds')
JCC_COND = {
    'jo': 'fl.of', 'jno': '!fl.of', 'jb': 'fl.cf', 'jae': '!fl.cf', 'jz': 'fl.zf',
    'jnz': '!fl.zf', 'jbe': '(fl.cf | fl.zf)', 'ja': '!(fl.cf | fl.zf)', 'js': 'fl.sf',
    'jns': '!fl.sf', 'jp': 'fl.pf', 'jnp': '!fl.pf', 'jl': '(fl.sf != fl.of)',
    'jge': '(fl.sf == fl.of)', 'jle': '(fl.zf | (fl.sf != fl.of))',
    'jg': '!(fl.zf | (fl.sf != fl.of))',
}
SHIFT_OP = {'rol': 0, 'ror': 1, 'rcl': 2, 'rcr': 3, 'shl': 4, 'shr': 5, 'sal': 6, 'sar': 7}
# Left to the emulator, one instruction at a time.
EMULATE = {'int', 'int3', 'into', 'iret', 'in', 'out', 'insb', 'insw', 'outsb', 'outsw',
           'cli', 'sti', 'hlt', 'esc', 'wait', 'daa', 'das', 'aaa', 'aas', 'aam', 'aad',
           'bound', 'salc', 'cmc', 'pusha', 'popa', 'enter', 'leave', 'lahf', 'sahf'}


class LiftError(Exception):
    pass


def fnv1a(data):
    h = 14695981039346656037
    for b in data:
        h = ((h ^ b) * 1099511628211) & 0xffffffffffffffff
    return h


class FunctionLifter:
    def __init__(self, unit, entry, entries):
        self.unit = unit
        self.entry = entry
        self.body, _ = D.function_body(unit, entry, entries)
        if not self.body:
            raise LiftError('no decoded body')
        cs = unit.cs.get(entry)
        if cs is None:
            raise LiftError('unknown CS base')
        self.cs_base = cs
        if any(unit.cs.get(o) != cs for o in self.body):
            raise LiftError('body spans several CS bases')
        self.lines = []
        self.labels = set()
        self.resume = [entry]

    # ------------------------------------------------------------ helpers
    def ip(self, off):
        return (off - self.cs_base) & 0xffff

    def emit(self, text):
        self.lines.append('    ' + text)

    def fixups_in(self, ins):
        return [f for f in self.unit.fixups if ins.offset <= f < ins.end or
                ins.offset <= f + 1 < ins.end]

    def imm(self, ins, op):
        """C expression for an immediate; relocated immediates come from memory."""
        fix = self.fixups_in(ins)
        if fix:
            pos = ins.end - op.enc
            if op.enc != 2 or fix != [pos]:
                raise LiftError('fixup outside an immediate word')
            return 'gw_rw(c, cs, 0x%04x)' % self.ip(pos)
        v = op.signed & 0xffff if op.enc < op.size else op.value
        return '0x%x' % (v & (0xff if op.size == 1 else 0xffff))

    @staticmethod
    def ea(m):
        parts = [] if m.base is None else m.base.split('+')
        if m.base is None:
            return '0x%04x' % (m.disp & 0xffff)
        expr = ' + '.join(parts)
        if m.disp:
            expr += ' + 0x%x' % (m.disp & 0xffff)
        return '(uint16_t)(%s)' % expr

    @staticmethod
    def seg(m, default=None):
        return m.seg or default or m.default_seg

    def read(self, ins, op, size=None):
        size = size or op.size
        if isinstance(op, X.Reg):
            return self.reg_read(op.name)
        if isinstance(op, X.Imm):
            return self.imm(ins, op)
        if isinstance(op, X.Mem):
            if self.fixups_in(ins) and not isinstance(ins.operands[-1], X.Imm):
                raise LiftError('fixup in a memory operand')
            fn = 'gw_rb' if size == 1 else 'gw_rw'
            return '%s(c, %s, %s)' % (fn, self.seg(op), self.ea(op))
        raise LiftError('operand %r' % (op,))

    @staticmethod
    def reg_read(name):
        if name in ('al', 'cl', 'dl', 'bl'):
            return '(uint8_t)%sx' % name[0]
        if name in ('ah', 'ch', 'dh', 'bh'):
            return '(uint8_t)(%sx >> 8)' % name[0]
        return name

    def write(self, ins, op, value, size=None):
        size = size or op.size
        if isinstance(op, X.Reg):
            n = op.name
            if n in ('al', 'cl', 'dl', 'bl'):
                return '%sx = (uint16_t)((%sx & 0xff00) | (uint8_t)(%s));' % (n[0], n[0], value)
            if n in ('ah', 'ch', 'dh', 'bh'):
                return '%sx = (uint16_t)((%sx & 0x00ff) | ((uint8_t)(%s) << 8));' % (
                    n[0], n[0], value)
            return '%s = (uint16_t)(%s);' % (n, value)
        if isinstance(op, X.Mem):
            fn = 'gw_wb' if size == 1 else 'gw_ww'
            cast = 'uint8_t' if size == 1 else 'uint16_t'
            return '%s(c, %s, %s, (%s)(%s));' % (fn, self.seg(op), self.ea(op), cast, value)
        raise LiftError('cannot write %r' % (op,))

    def goto(self, target, exit_kind='GW_LIFT_EXIT'):
        if target in self.body and target in self.unit.insns:
            self.labels.add(target)
            return 'goto L_%x;' % target
        return 'ip = 0x%04x; rc = %s; goto out;' % (self.ip(target), exit_kind)

    def push(self, value):
        return 'sp = (uint16_t)(sp - 2); gw_ww(c, ss, sp, (uint16_t)(%s));' % value

    def pop(self, dest):
        return '%s = gw_rw(c, ss, sp); sp = (uint16_t)(sp + 2);' % dest

    def exit_to(self, ip_expr, kind='GW_LIFT_EXIT'):
        return 'ip = (uint16_t)(%s); rc = %s; goto out;' % (ip_expr, kind)

    def add_resume(self, off):
        if off in self.body and off not in self.resume:
            self.resume.append(off)

    # ----------------------------------------------------------- translate
    def lift(self):
        order = sorted(self.body)
        for i, off in enumerate(order):
            ins = self.unit.insns[off]
            self.lines.append(('label', off))
            self.emit('/* %04x: %s */' % (self.ip(off), X.format_text(ins).replace('*/', '* /')))
            falls = self.instruction(ins)
            if falls:
                nxt = order[i + 1] if i + 1 < len(order) else None
                if nxt != ins.end:
                    self.emit(self.goto(ins.end))
        return self

    def instruction(self, ins):
        """Emit C for one instruction; returns True if it can fall through."""
        mn, ops, end = ins.mnemonic, ins.operands, ins.end
        size = next((o.size for o in ops if isinstance(o, (X.Reg, X.Mem)) and o.size in (1, 2)),
                    2)
        bits = 8 * size
        if mn in EMULATE or ins.note in ('x87', 'fp-emulator'):
            self.emit(self.exit_to('0x%04x' % self.ip(ins.offset), 'GW_LIFT_EMULATE'))
            self.add_resume(end)
            return False
        if mn == 'nop':
            return True
        if mn == 'mov':
            dst, src = ops
            self.emit(self.write(ins, dst, self.read(ins, src)))
            return True
        if mn in ('add', 'adc', 'sub', 'sbb', 'cmp', 'and', 'or', 'xor', 'test'):
            dst, src = ops
            a, b = self.read(ins, dst), self.read(ins, src)
            if mn in ('add', 'adc'):
                expr = 'gw_add(&fl, %s, %s, %s, %d)' % (a, b, 'fl.cf' if mn == 'adc' else '0',
                                                        bits)
            elif mn in ('sub', 'sbb', 'cmp'):
                expr = 'gw_sub(&fl, %s, %s, %s, %d)' % (a, b, 'fl.cf' if mn == 'sbb' else '0',
                                                        bits)
            else:
                o = {'and': '&', 'test': '&', 'or': '|', 'xor': '^'}[mn]
                expr = 'gw_logic(&fl, (uint32_t)(%s) %s (uint32_t)(%s), %d)' % (a, o, b, bits)
            if mn in ('cmp', 'test'):
                self.emit('(void)%s;' % expr)
            else:
                self.emit('{ uint32_t r = %s; %s }' % (expr, self.write(ins, dst, 'r')))
            return True
        if mn in ('inc', 'dec', 'neg', 'not'):
            (dst,) = ops
            a = self.read(ins, dst)
            if mn == 'not':
                self.emit(self.write(ins, dst, '~(uint32_t)(%s)' % a))
                return True
            if mn == 'neg':
                self.emit('{ uint32_t v = %s; uint32_t r = gw_sub(&fl, 0, v, 0, %d); '
                          'fl.cf = v != 0; %s }' % (a, bits, self.write(ins, dst, 'r')))
                return True
            self.emit('{ uint32_t r = gw_%s(&fl, %s, %d); %s }' % (mn, a, bits,
                                                                   self.write(ins, dst, 'r')))
            return True
        if mn in SHIFT_OP:
            dst, cnt = ops
            n = '((unsigned)(uint8_t)cx & 0x1f)' if isinstance(cnt, X.Reg) else \
                str(cnt.value & 0x1f)
            self.emit('{ uint32_t r = gw_shift(&fl, %d, %s, %s, %d); %s }' % (
                SHIFT_OP[mn], self.read(ins, dst), n, bits, self.write(ins, dst, 'r')))
            return True
        if mn == 'push':
            (src,) = ops
            if isinstance(src, X.Reg) and src.name == 'sp':
                self.emit(self.push('sp'))      # 286+: pushes the old SP
            else:
                self.emit(self.push(self.read(ins, src)))
            return True
        if mn == 'pop':
            (dst,) = ops
            if isinstance(dst, X.Reg):
                self.emit(self.pop(dst.name))
            else:
                self.emit('{ uint16_t v; %s %s }' % (self.pop('v'), self.write(ins, dst, 'v')))
            return True
        if mn == 'pushf':
            self.emit(self.push('gw_pack(c->flags, &fl)'))
            return True
        if mn == 'popf':
            self.emit('{ uint16_t v; %s c->flags = v; fl = gw_unpack(v); }' % self.pop('v'))
            return True
        if mn == 'xchg':
            a, b = ops
            self.emit('{ uint32_t t = %s; %s %s }' % (
                self.read(ins, a), self.write(ins, a, self.read(ins, b)),
                self.write(ins, b, 't')))
            return True
        if mn == 'lea':
            dst, m = ops
            self.emit(self.write(ins, dst, self.ea(m)))
            return True
        if mn in ('les', 'lds'):
            dst, m = ops
            s = self.seg(m)
            self.emit('{ uint16_t o = %s; uint16_t sg = gw_rw(c, %s, (uint16_t)(%s + 2)); '
                      '%s %s = sg; }' % (self.read(ins, m, 2), s, self.ea(m),
                                         self.write(ins, dst, 'o'), mn[1:] + ''))
            return True
        if mn == 'cbw':
            self.emit('ax = (uint16_t)(int16_t)(int8_t)(uint8_t)ax;')
            return True
        if mn == 'cwd':
            self.emit('dx = (ax & 0x8000) ? 0xffff : 0;')
            return True
        if mn in ('clc', 'stc', 'cld', 'std'):
            self.emit('fl.%s = %d;' % ('cf' if mn[2] == 'c' else 'df', mn[0:2] == 'st'))
            return True
        if mn == 'xlatb':
            self.emit('ax = (uint16_t)((ax & 0xff00) | gw_rb(c, %s, (uint16_t)(bx + (uint8_t)ax)));'
                      % (ins.seg or 'ds'))
            return True
        if mn in ('mul', 'imul', 'div', 'idiv'):
            return self.muldiv(ins, size)
        if mn.startswith(('movs', 'stos', 'lods', 'scas', 'cmps')):
            return self.string(ins)
        if ins.flow == 'jcc':
            if mn in JCC_COND:
                cond = JCC_COND[mn]
            elif mn == 'jcxz':
                cond = 'cx == 0'
            else:
                pre = 'cx = (uint16_t)(cx - 1); '
                cond = {'loop': 'cx != 0', 'loope': '(cx != 0 && fl.zf)',
                        'loopne': '(cx != 0 && !fl.zf)'}[mn]
                self.emit(pre.strip())
            self.emit('if (%s) { %s }' % (cond, self.goto(ins.target)))
            return True
        if mn == 'jmp' and ins.flow == 'jmp':
            self.emit(self.goto(ins.target))
            return False
        if mn == 'call' and ins.flow == 'call':
            self.emit(self.push('0x%04x' % self.ip(end)))
            self.emit(self.exit_to('0x%04x' % self.ip(ins.target)))
            self.add_resume(end)
            return False
        if ins.flow in ('callfar', 'jmpfar'):
            # Read the pointer from memory: RTLink relocated it.
            p = self.ip(ins.end - 4)
            self.emit('{ uint16_t o = gw_rw(c, cs, 0x%04x), sg = gw_rw(c, cs, 0x%04x);' % (
                p, (p + 2) & 0xffff))
            if ins.flow == 'callfar':
                self.emit('  %s %s' % (self.push('cs'), self.push('0x%04x' % self.ip(end))))
                self.add_resume(end)
            self.emit('  cs = sg; %s }' % self.exit_to('o'))
            return False
        if ins.flow == 'ret':
            far = mn == 'retf'
            extra = ops[0].value if ops else 0
            self.emit('{ uint16_t o; %s%s sp = (uint16_t)(sp + %d); %s }' % (
                self.pop('o'), (' ' + self.pop('cs')) if far else '', extra, self.exit_to('o')))
            return False
        if ins.flow == 'jmpind':
            return self.jump_indirect(ins)
        if ins.flow == 'callind':
            (m,) = ops
            if ins.note == 'far':
                s = self.seg(m) if isinstance(m, X.Mem) else None
                self.emit('{ uint16_t o = %s, sg = gw_rw(c, %s, (uint16_t)(%s + 2));' % (
                    self.read(ins, m, 2), s, self.ea(m)))
                self.emit('  %s %s cs = sg; %s }' % (
                    self.push('cs'), self.push('0x%04x' % self.ip(end)), self.exit_to('o')))
            else:
                self.emit('{ uint16_t o = %s; %s %s }' % (
                    self.read(ins, m, 2), self.push('0x%04x' % self.ip(end)), self.exit_to('o')))
            self.add_resume(end)
            return False
        raise LiftError('no translation for %s at %s' % (mn, self.unit.ident(ins.offset)))

    def muldiv(self, ins, size):
        mn = ins.mnemonic
        ops = ins.operands
        if mn == 'imul' and len(ops) == 3:
            dst, src, imm = ops
            self.emit('{ int32_t p = (int32_t)(int16_t)%s * (int32_t)(int16_t)%s; '
                      'fl.cf = fl.of = p != (int16_t)p; gw_szp(&fl, (uint32_t)p, 16); fl.af = 0; '
                      '%s }' % (self.read(ins, src), self.imm(ins, imm),
                                self.write(ins, dst, '(uint16_t)p')))
            return True
        (src,) = ops
        s = self.read(ins, src)
        here = '0x%04x' % self.ip(ins.offset)
        if size == 1:
            if mn == 'mul':
                self.emit('{ uint16_t p = (uint16_t)((uint8_t)ax * (uint8_t)%s); ax = p; '
                          'fl.cf = fl.of = (p >> 8) != 0; gw_szp(&fl, p, 8); fl.af = 0; }' % s)
            elif mn == 'imul':
                self.emit('{ int16_t p = (int16_t)((int8_t)(uint8_t)ax * (int8_t)(uint8_t)%s); '
                          'ax = (uint16_t)p; fl.cf = fl.of = p != (int8_t)p; '
                          'gw_szp(&fl, (uint16_t)p, 8); fl.af = 0; }' % s)
            elif mn == 'div':
                self.emit('{ uint8_t d = %s; uint16_t n = ax; if (!d || n / d > 0xff) { %s }'
                          ' ax = (uint16_t)(((n %% d) << 8) | (n / d)); }' % (
                              s, self.exit_to(here, 'GW_LIFT_EMULATE')))
            else:
                self.emit('{ int8_t d = (int8_t)%s; int16_t n = (int16_t)ax; int q; '
                          'if (!d || (q = n / d) > 127 || q < -128) { %s }'
                          ' ax = (uint16_t)(((uint8_t)(n %% d) << 8) | (uint8_t)q); }' % (
                              s, self.exit_to(here, 'GW_LIFT_EMULATE')))
        else:
            if mn == 'mul':
                self.emit('{ uint32_t p = (uint32_t)ax * (uint16_t)%s; ax = (uint16_t)p; '
                          'dx = (uint16_t)(p >> 16); fl.cf = fl.of = dx != 0; '
                          'gw_szp(&fl, p, 16); fl.af = 0; }' % s)
            elif mn == 'imul':
                self.emit('{ int32_t p = (int32_t)(int16_t)ax * (int16_t)%s; ax = (uint16_t)p; '
                          'dx = (uint16_t)((uint32_t)p >> 16); fl.cf = fl.of = p != (int16_t)p; '
                          'gw_szp(&fl, (uint32_t)p, 16); fl.af = 0; }' % s)
            elif mn == 'div':
                self.emit('{ uint16_t d = %s; uint32_t n = ((uint32_t)dx << 16) | ax; '
                          'if (!d || n / d > 0xffff) { %s } ax = (uint16_t)(n / d); '
                          'dx = (uint16_t)(n %% d); }' % (s, self.exit_to(here, 'GW_LIFT_EMULATE')))
            else:
                self.emit('{ int32_t d = (int16_t)%s; int32_t n = (int32_t)(((uint32_t)dx << 16) | ax);'
                          ' int32_t q; if (!d || (n == INT32_MIN && d == -1) || (q = n / d) > 32767 '
                          '|| q < -32768) { %s } ax = (uint16_t)q; dx = (uint16_t)(n %% d); }' % (
                              s, self.exit_to(here, 'GW_LIFT_EMULATE')))
        self.add_resume(ins.end)
        return True

    def string(self, ins):
        mn = ins.mnemonic
        w = mn.endswith('w')
        n = 2 if w else 1
        src_seg = ins.seg or 'ds'
        step = '(fl.df ? -%d : %d)' % (n, n)
        rd = 'gw_rw' if w else 'gw_rb'
        wr = 'gw_ww' if w else 'gw_wb'
        acc_r = 'ax' if w else '(uint8_t)ax'
        acc_w = 'ax = v;' if w else 'ax = (uint16_t)((ax & 0xff00) | v);'
        kind = mn[:4]
        body = {
            'movs': '%s(c, es, di, %s(c, %s, si)); si = (uint16_t)(si + %s); '
                    'di = (uint16_t)(di + %s);' % (wr, rd, src_seg, step, step),
            'stos': '%s(c, es, di, %s); di = (uint16_t)(di + %s);' % (wr, acc_r, step),
            'lods': '{ uint16_t v = %s(c, %s, si); %s } si = (uint16_t)(si + %s);' % (
                rd, src_seg, acc_w, step),
            'scas': '(void)gw_sub(&fl, %s, %s(c, es, di), 0, %d); di = (uint16_t)(di + %s);' % (
                acc_r, rd, 8 * n, step),
            'cmps': '(void)gw_sub(&fl, %s(c, %s, si), %s(c, es, di), 0, %d); '
                    'si = (uint16_t)(si + %s); di = (uint16_t)(di + %s);' % (
                        rd, src_seg, rd, 8 * n, step, step),
        }[kind]
        if ins.rep is None:
            self.emit(body)
            return True
        stop = ''
        if kind in ('scas', 'cmps'):
            # F3 = repe, F2 = repne for the comparing forms.
            stop = ' if (%sfl.zf) break;' % ('!' if ins.rep == 'rep' else '')
        self.emit('while (cx) { %s cx = (uint16_t)(cx - 1);%s }' % (body, stop))
        return True

    def jump_indirect(self, ins):
        (m,) = ins.operands
        table = next((t for t in self.unit.tables.items() if t[1][2] == ins.offset), None)
        if ins.note == 'far':
            self.emit('{ uint16_t o = %s, sg = gw_rw(c, %s, (uint16_t)(%s + 2)); cs = sg; %s }' % (
                self.read(ins, m, 2), self.seg(m), self.ea(m), self.exit_to('o')))
            return False
        self.emit('{ uint16_t t = %s;' % self.read(ins, m, 2))
        if table is not None:
            self.emit('  switch (t) {')
            for t in sorted(set(table[1][1])):
                self.emit('  case 0x%04x: %s' % (self.ip(t), self.goto(t)))
            self.emit('  default: break; }')
        self.emit('  %s }' % self.exit_to('t'))
        return False

    # ---------------------------------------------------------------- output
    def linked_segment(self):
        linear = self.unit.linear_base + self.cs_base
        if linear & 15:
            raise LiftError('CS base not paragraph aligned')
        return linear >> 4

    def name(self):
        return 'lift_%s_%x' % (self.unit.name.replace('-', '_'), self.entry)

    def c_source(self):
        out = ['/* %s */' % self.unit.ident(self.entry),
               'static int %s(gw_cpu *c) {' % self.name(),
               '    uint16_t ax = c->ax, cx = c->cx, dx = c->dx, bx = c->bx, sp = c->sp, '
               'bp = c->bp, si = c->si, di = c->di;',
               '    uint16_t es = c->es, cs = c->cs, ss = c->ss, ds = c->ds, ip;',
               '    gw_flags fl = gw_unpack(c->flags);',
               '    int rc;',
               '    switch (c->ip) {']
        for r in self.resume:
            self.labels.add(r)
            out.append('    case 0x%04x: goto L_%x;' % (self.ip(r), r))
        out.append('    default: return GW_LIFT_BAD_ENTRY;')
        out.append('    }')
        for line in self.lines:
            if isinstance(line, tuple):
                if line[1] in self.labels:
                    out.append('L_%x:' % line[1])
            else:
                out.append(line)
        out += ['out:',
                '    c->ax = ax; c->cx = cx; c->dx = dx; c->bx = bx; c->sp = sp; c->bp = bp; '
                'c->si = si; c->di = di;',
                '    c->es = es; c->cs = cs; c->ss = ss; c->ds = ds; c->ip = ip;',
                '    c->flags = gw_pack(c->flags, &fl);',
                '    return rc;',
                '}', '']
        return '\n'.join(out)

    def metadata(self):
        body = sorted(self.body)
        ranges = []
        for off in body:
            end = self.unit.insns[off].end
            if ranges and ranges[-1][1] == off:
                ranges[-1][1] = end
            else:
                ranges.append([off, end])
        fix = sorted({f for f in self.unit.fixups
                      if any(a <= f < b for a, b in ranges)})
        masked = set()
        for f in fix:
            masked.update((f, f + 1))
        data = bytearray()
        for a, b in ranges:
            data += bytes(0 if o in masked else self.unit.data[o] for o in range(a, b))
        return ranges, fix, fnv1a(data)


def c_module(unit, lifted, failed):
    out = ['/* Generated by tools/lift_x86.py from %s. Contains a translation of original' % unit.name,
           ' * program code: keep under build/, never commit. */',
           '#include "lift_abi.h"', '#include <stdint.h>', '']
    for f in lifted:
        out.append(f.c_source())
    table = []
    for f in lifted:
        ranges, fix, fp = f.metadata()
        n = f.name()
        out.append('static const uint16_t %s_resume[] = {%s};' % (
            n, ', '.join('0x%04x' % f.ip(r) for r in f.resume)))
        out.append('static const uint16_t %s_ranges[][2] = {%s};' % (
            n, ', '.join('{0x%04x, 0x%04x}' % (f.ip(a), f.ip(b)) for a, b in ranges)))
        out.append('static const uint16_t %s_fixups[] = {%s};' % (
            n, ', '.join('0x%04x' % f.ip(x) for x in fix) or '0'))
        table.append('    {"%s", "%s", %d, 0x%04x, 0x%04x, %s_resume, %d, %s_ranges, %d, '
                     '%s_fixups, %d, 0x%016xull, %s},' % (
                         unit.ident(f.entry), unit.name,
                         -1 if unit.section is None else unit.section, f.linked_segment(),
                         f.ip(f.entry), n, len(f.resume), n, len(ranges), n, len(fix), fp, n))
    out.append('')
    for ident, why in failed:
        out.append('/* not lifted: %s: %s */' % (ident, why))
    out.append('static const gw_lift_function functions[] = {')
    out += table or ['    {0}']
    out.append('};')
    out.append('')
    out.append('const gw_lift_module gw_lift_module_%s = {GW_LIFT_ABI_VERSION, "%s", %d, '
               'functions};' % (unit.name.replace('-', '_'), unit.name, len(lifted)))
    out.append('const gw_lift_module *gw_lift_entry(void) { return &gw_lift_module_%s; }' %
               unit.name.replace('-', '_'))
    return '\n'.join(out) + '\n'


def lift_unit(unit, only=None):
    entries = {o for o in unit.functions if o in unit.insns}
    lifted, failed = [], []
    for entry in sorted(entries):
        if only is not None and entry not in only:
            continue
        try:
            lifted.append(FunctionLifter(unit, entry, entries).lift())
        except LiftError as exc:
            failed.append((unit.ident(entry), str(exc)))
    return lifted, failed


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('exe')
    ap.add_argument('ovl')
    ap.add_argument('--rtl')
    ap.add_argument('--manifest', required=True)
    ap.add_argument('--tracker', default=str(pathlib.Path(__file__).resolve().parents[1]
                                             / 'decomp' / 'functions.tsv'))
    ap.add_argument('--unit', required=True, help='unit name, e.g. ovl-038')
    ap.add_argument('--output', required=True)
    args = ap.parse_args(argv)
    prog, _, _ = D.analyze(args.exe, args.ovl, args.manifest, args.rtl, args.tracker)
    unit = next((u for u in prog.units if u.name == args.unit), None)
    if unit is None:
        ap.error('unknown unit %s' % args.unit)
    lifted, failed = lift_unit(unit)
    out = pathlib.Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(c_module(unit, lifted, failed))
    print('%s: %d functions lifted, %d not lifted' % (unit.name, len(lifted), len(failed)))
    for ident, why in failed:
        print('  %s: %s' % (ident, why))
    return 0


if __name__ == '__main__':
    sys.exit(main())
