# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Table-driven 8086/80186/80286 real-mode decoder with exact NASM output.

The decoder returns structured instructions (for later lifting) and can
format each one as NASM source that reassembles to the *same bytes*.
Where NASM would choose another encoding (register direction bit, short
accumulator forms, redundant prefixes...), the formatter uses a prelude
macro or falls back to ``db`` with the instruction text as a comment.
Callers must still verify the assembled result; this module only promises
best effort per instruction.
"""
from dataclasses import dataclass, field

R8 = ('al', 'cl', 'dl', 'bl', 'ah', 'ch', 'dh', 'bh')
R16 = ('ax', 'cx', 'dx', 'bx', 'sp', 'bp', 'si', 'di')
SREG = ('es', 'cs', 'ss', 'ds')
RM16 = ('bx+si', 'bx+di', 'bp+si', 'bp+di', 'si', 'di', 'bp', 'bx')
ALU = ('add', 'or', 'adc', 'sbb', 'and', 'sub', 'xor', 'cmp')
SHIFT = ('rol', 'ror', 'rcl', 'rcr', 'shl', 'shr', 'sal', 'sar')
JCC = ('jo', 'jno', 'jb', 'jae', 'jz', 'jnz', 'jbe', 'ja',
       'js', 'jns', 'jp', 'jnp', 'jl', 'jge', 'jle', 'jg')
PREFIX_SEG = {0x26: 'es', 0x2e: 'cs', 0x36: 'ss', 0x3e: 'ds'}
STRING_OPS = {0xa4: 'movsb', 0xa5: 'movsw', 0xa6: 'cmpsb', 0xa7: 'cmpsw',
              0xaa: 'stosb', 0xab: 'stosw', 0xac: 'lodsb', 0xad: 'lodsw',
              0xae: 'scasb', 0xaf: 'scasw', 0x6c: 'insb', 0x6d: 'insw',
              0x6e: 'outsb', 0x6f: 'outsw'}
SIMPLE = {0x27: 'daa', 0x2f: 'das', 0x37: 'aaa', 0x3f: 'aas', 0x60: 'pusha',
          0x61: 'popa', 0x90: 'nop', 0x98: 'cbw', 0x99: 'cwd', 0x9b: 'wait',
          0x9c: 'pushf', 0x9d: 'popf', 0x9e: 'sahf', 0x9f: 'lahf', 0xc9: 'leave',
          0xcc: 'int3', 0xce: 'into', 0xd7: 'xlatb', 0xf4: 'hlt', 0xf5: 'cmc',
          0xf8: 'clc', 0xf9: 'stc', 0xfa: 'cli', 0xfb: 'sti', 0xfc: 'cld',
          0xfd: 'std'}


class DecodeError(Exception):
    pass


@dataclass(frozen=True)
class Reg:
    name: str
    size: int  # 1, 2 (sreg: 2)


@dataclass(frozen=True)
class Imm:
    value: int        # as encoded, unsigned
    size: int         # operand size in bytes
    enc: int          # encoded bytes (1 for sign-extended imm8)

    @property
    def signed(self):
        bits = self.enc * 8
        v = self.value & ((1 << bits) - 1)
        return v - (1 << bits) if v >> (bits - 1) else v


@dataclass(frozen=True)
class Mem:
    base: str | None      # one of RM16 or None for direct address
    disp: int             # signed for base forms, unsigned for direct
    disp_size: int        # 0, 1 or 2 bytes
    size: int             # 1, 2, 4 (far pointer), 0 (lea/unsized)
    seg: str | None       # explicit override prefix

    @property
    def default_seg(self):
        return 'ss' if self.base and self.base.startswith('bp') else 'ds'


@dataclass(frozen=True)
class Far:
    seg: int
    off: int


@dataclass(frozen=True)
class Rel:
    target: int   # absolute image offset
    size: int     # 1 or 2


@dataclass
class Instr:
    offset: int
    length: int
    raw: bytes
    mnemonic: str
    operands: tuple = ()
    prefixes: tuple = ()          # raw prefix bytes in order
    seg: str | None = None        # segment override (if any)
    rep: str | None = None        # 'rep', 'repe', 'repne'
    opcode: int = 0
    modrm: int | None = None
    flow: str = 'next'            # next, jmp, jcc, call, ret, callfar, jmpfar,
                                  # jmpind, callind, stop
    target: int | None = None     # near target (image offset) for rel flows
    far: Far | None = None
    note: str = ''
    extra: dict = field(default_factory=dict)

    @property
    def end(self):
        return self.offset + self.length


class _Reader:
    def __init__(self, data, pos, limit):
        self.data, self.pos, self.limit = data, pos, limit

    def u8(self):
        if self.pos >= self.limit:
            raise DecodeError('truncated instruction')
        v = self.data[self.pos]
        self.pos += 1
        return v

    def u16(self):
        lo = self.u8()
        return lo | (self.u8() << 8)

    def s8(self):
        v = self.u8()
        return v - 256 if v & 0x80 else v


def _modrm(rd, seg, size):
    m = rd.u8()
    mod, reg, rm = m >> 6, (m >> 3) & 7, m & 7
    if mod == 3:
        return m, reg, Reg((R8 if size == 1 else R16)[rm], size)
    if mod == 0 and rm == 6:
        return m, reg, Mem(None, rd.u16(), 2, size, seg)
    if mod == 0:
        return m, reg, Mem(RM16[rm], 0, 0, size, seg)
    if mod == 1:
        return m, reg, Mem(RM16[rm], rd.s8(), 1, size, seg)
    d = rd.u16()
    return m, reg, Mem(RM16[rm], d - 0x10000 if d & 0x8000 else d, 2, size, seg)


def _greg(reg, size):
    return Reg((R8 if size == 1 else R16)[reg], size)


def decode(data, pos, limit=None):
    """Decode one instruction at ``data[pos]``. Offsets are image offsets."""
    limit = len(data) if limit is None else limit
    rd = _Reader(data, pos, limit)
    prefixes, seg, rep = [], None, None
    while True:
        b = rd.u8()
        if b in PREFIX_SEG:
            prefixes.append(b)
            seg = PREFIX_SEG[b]
        elif b in (0xf2, 0xf3, 0xf0):
            prefixes.append(b)
            if b != 0xf0:
                rep = b
        else:
            break
        if len(prefixes) > 4:
            raise DecodeError('too many prefixes')
    op = b
    ins = dict(mnemonic=None, operands=(), flow='next', target=None, far=None,
               modrm=None, note='')

    def done():
        raw = bytes(data[pos:rd.pos])
        repname = None
        if rep is not None:
            repname = 'repne' if rep == 0xf2 else 'rep'
        return Instr(pos, rd.pos - pos, raw, ins['mnemonic'], tuple(ins['operands']),
                     tuple(prefixes), seg, repname, op, ins['modrm'], ins['flow'],
                     ins['target'], ins['far'], ins['note'])

    def set_(mn, *ops, flow='next'):
        ins['mnemonic'], ins['operands'], ins['flow'] = mn, ops, flow

    if op < 0x40 and (op & 7) < 6:
        alu, form = ALU[op >> 3], op & 7
        if form < 4:
            size = 1 if form in (0, 2) else 2
            m, reg, rm = _modrm(rd, seg, size)
            ins['modrm'] = m
            g = _greg(reg, size)
            set_(alu, *((rm, g) if form < 2 else (g, rm)))
        elif form == 4:
            set_(alu, Reg('al', 1), Imm(rd.u8(), 1, 1))
        else:
            set_(alu, Reg('ax', 2), Imm(rd.u16(), 2, 2))
        return done()
    if op in (0x06, 0x0e, 0x16, 0x1e):
        set_('push', Reg(SREG[op >> 3], 2))
        return done()
    if op in (0x07, 0x17, 0x1f):
        set_('pop', Reg(SREG[op >> 3], 2))
        return done()
    if op in SIMPLE:
        set_(SIMPLE[op], flow='stop' if op == 0xf4 else 'next')
        return done()
    if 0x40 <= op < 0x60:
        set_(('inc', 'dec', 'push', 'pop')[(op - 0x40) >> 3], Reg(R16[op & 7], 2))
        return done()
    if op == 0x62:
        m, reg, rm = _modrm(rd, seg, 4)
        ins['modrm'] = m
        if isinstance(rm, Reg):
            raise DecodeError('bound with register operand')
        set_('bound', _greg(reg, 2), rm)
        return done()
    if op == 0x68:
        set_('push', Imm(rd.u16(), 2, 2))
        return done()
    if op == 0x6a:
        set_('push', Imm(rd.u8(), 2, 1))
        return done()
    if op in (0x69, 0x6b):
        m, reg, rm = _modrm(rd, seg, 2)
        ins['modrm'] = m
        imm = Imm(rd.u16(), 2, 2) if op == 0x69 else Imm(rd.u8(), 2, 1)
        set_('imul', _greg(reg, 2), rm, imm)
        return done()
    if op in STRING_OPS:
        set_(STRING_OPS[op])
        if rep is not None and op in (0xa6, 0xa7, 0xae, 0xaf):
            ins['note'] = 'repe' if rep == 0xf3 else 'repne'
        return done()
    if 0x70 <= op < 0x80 or 0xe0 <= op <= 0xe3:
        d = rd.s8()
        name = JCC[op - 0x70] if op < 0x80 else ('loopne', 'loope', 'loop', 'jcxz')[op - 0xe0]
        ins['target'] = (rd.pos + d) & 0xffffffff
        set_(name, Rel(rd.pos + d, 1), flow='jcc')
        return done()
    if 0x80 <= op <= 0x83:
        size = 1 if op in (0x80, 0x82) else 2
        m, reg, rm = _modrm(rd, seg, size)
        ins['modrm'] = m
        if op == 0x81:
            imm = Imm(rd.u16(), 2, 2)
        elif op == 0x83:
            imm = Imm(rd.u8(), 2, 1)
        else:
            imm = Imm(rd.u8(), 1, 1)
        set_(ALU[reg], rm, imm)
        if op == 0x82:
            ins['note'] = 'alias-82'
        return done()
    if 0x84 <= op <= 0x8b:
        size = 1 if op % 2 == 0 else 2
        m, reg, rm = _modrm(rd, seg, size)
        ins['modrm'] = m
        g = _greg(reg, size)
        name = ('test', 'test', 'xchg', 'xchg', 'mov', 'mov', 'mov', 'mov')[op - 0x84]
        set_(name, *((g, rm) if op in (0x8a, 0x8b) else (rm, g)))
        return done()
    if op in (0x8c, 0x8e):
        m, reg, rm = _modrm(rd, seg, 2)
        ins['modrm'] = m
        if reg > 3:
            raise DecodeError('bad segment register')
        s = Reg(SREG[reg], 2)
        set_('mov', *((rm, s) if op == 0x8c else (s, rm)))
        return done()
    if op == 0x8d:
        m, reg, rm = _modrm(rd, seg, 0)
        ins['modrm'] = m
        if isinstance(rm, Reg):
            raise DecodeError('lea with register operand')
        set_('lea', _greg(reg, 2), rm)
        return done()
    if op == 0x8f:
        m, reg, rm = _modrm(rd, seg, 2)
        ins['modrm'] = m
        if reg:
            raise DecodeError('bad 8f extension')
        set_('pop', rm)
        return done()
    if 0x91 <= op <= 0x97:
        set_('xchg', Reg('ax', 2), Reg(R16[op & 7], 2))
        return done()
    if op in (0x9a, 0xea):
        off = rd.u16()
        sg = rd.u16()
        ins['far'] = Far(sg, off)
        set_('call' if op == 0x9a else 'jmp', Far(sg, off),
             flow='callfar' if op == 0x9a else 'jmpfar')
        return done()
    if 0xa0 <= op <= 0xa3:
        size = 1 if op % 2 == 0 else 2
        mem = Mem(None, rd.u16(), 2, size, seg)
        acc = Reg('al' if size == 1 else 'ax', size)
        set_('mov', *((acc, mem) if op < 0xa2 else (mem, acc)))
        return done()
    if op == 0xa8:
        set_('test', Reg('al', 1), Imm(rd.u8(), 1, 1))
        return done()
    if op == 0xa9:
        set_('test', Reg('ax', 2), Imm(rd.u16(), 2, 2))
        return done()
    if 0xb0 <= op < 0xb8:
        set_('mov', Reg(R8[op & 7], 1), Imm(rd.u8(), 1, 1))
        return done()
    if 0xb8 <= op < 0xc0:
        set_('mov', Reg(R16[op & 7], 2), Imm(rd.u16(), 2, 2))
        return done()
    if op in (0xc0, 0xc1, 0xd0, 0xd1, 0xd2, 0xd3):
        size = 1 if op % 2 == 0 else 2
        m, reg, rm = _modrm(rd, seg, size)
        ins['modrm'] = m
        if op in (0xc0, 0xc1):
            cnt = Imm(rd.u8(), 1, 1)
        elif op in (0xd0, 0xd1):
            cnt = Imm(1, 1, 0)
        else:
            cnt = Reg('cl', 1)
        set_(SHIFT[reg], rm, cnt)
        if reg == 6:
            ins['note'] = 'sal-6'
        return done()
    if op in (0xc2, 0xca):
        set_('ret' if op == 0xc2 else 'retf', Imm(rd.u16(), 2, 2), flow='ret')
        return done()
    if op in (0xc3, 0xcb):
        set_('ret' if op == 0xc3 else 'retf', flow='ret')
        return done()
    if op == 0xcf:
        set_('iret', flow='ret')
        return done()
    if op in (0xc4, 0xc5):
        m, reg, rm = _modrm(rd, seg, 4)
        ins['modrm'] = m
        if isinstance(rm, Reg):
            raise DecodeError('les/lds with register operand')
        set_('les' if op == 0xc4 else 'lds', _greg(reg, 2), rm)
        return done()
    if op in (0xc6, 0xc7):
        size = 1 if op == 0xc6 else 2
        m, reg, rm = _modrm(rd, seg, size)
        ins['modrm'] = m
        if reg:
            raise DecodeError('bad c6/c7 extension')
        set_('mov', rm, Imm(rd.u8(), 1, 1) if size == 1 else Imm(rd.u16(), 2, 2))
        return done()
    if op == 0xc8:
        a = rd.u16()
        set_('enter', Imm(a, 2, 2), Imm(rd.u8(), 1, 1))
        return done()
    if op == 0xcd:
        n = rd.u8()
        set_('int', Imm(n, 1, 1))
        # Microsoft C floating-point emulator: INT 34h-3Bh replace ESC
        # D8-DF and carry the ModR/M operand; INT 3Ch carries a segment
        # override byte before it; INT 3Dh is FWAIT.
        if 0x34 <= n <= 0x3b or n == 0x3c:
            if n == 0x3c:
                rd.u8()
            _modrm(rd, None, 0)
            ins['note'] = 'fp-emulator'
        elif n == 0x3d:
            ins['note'] = 'fp-emulator'
        return done()
    if op in (0xd4, 0xd5):
        set_('aam' if op == 0xd4 else 'aad', Imm(rd.u8(), 1, 1))
        return done()
    if op == 0xd6:
        set_('salc')
        return done()
    if 0xd8 <= op <= 0xdf:
        m, reg, rm = _modrm(rd, seg, 0)
        ins['modrm'] = m
        set_('esc', Imm(((op & 7) << 3) | reg, 1, 1), rm)
        ins['note'] = 'x87'
        return done()
    if op in (0xe4, 0xe5):
        set_('in', Reg('al' if op == 0xe4 else 'ax', 1 if op == 0xe4 else 2), Imm(rd.u8(), 1, 1))
        return done()
    if op in (0xe6, 0xe7):
        set_('out', Imm(rd.u8(), 1, 1), Reg('al' if op == 0xe6 else 'ax', 1 if op == 0xe6 else 2))
        return done()
    if op in (0xec, 0xed):
        set_('in', Reg('al' if op == 0xec else 'ax', 1), Reg('dx', 2))
        return done()
    if op in (0xee, 0xef):
        set_('out', Reg('dx', 2), Reg('al' if op == 0xee else 'ax', 1))
        return done()
    if op == 0xe8:
        d = rd.u16()
        t = (rd.pos + (d - 0x10000 if d & 0x8000 else d))
        ins['target'] = t
        set_('call', Rel(t, 2), flow='call')
        return done()
    if op == 0xe9:
        d = rd.u16()
        t = (rd.pos + (d - 0x10000 if d & 0x8000 else d))
        ins['target'] = t
        set_('jmp', Rel(t, 2), flow='jmp')
        return done()
    if op == 0xeb:
        d = rd.s8()
        ins['target'] = rd.pos + d
        set_('jmp', Rel(rd.pos + d, 1), flow='jmp')
        return done()
    if op in (0xf6, 0xf7):
        size = 1 if op == 0xf6 else 2
        m, reg, rm = _modrm(rd, seg, size)
        ins['modrm'] = m
        if reg in (0, 1):
            imm = Imm(rd.u8(), 1, 1) if size == 1 else Imm(rd.u16(), 2, 2)
            set_('test', rm, imm)
            if reg == 1:
                ins['note'] = 'test-1'
        else:
            set_(('', '', 'not', 'neg', 'mul', 'imul', 'div', 'idiv')[reg], rm)
        return done()
    if op == 0xfe:
        m, reg, rm = _modrm(rd, seg, 1)
        ins['modrm'] = m
        if reg > 1:
            raise DecodeError('bad fe extension')
        set_('inc' if reg == 0 else 'dec', rm)
        return done()
    if op == 0xff:
        m, reg, rm = _modrm(rd, seg, 2)
        ins['modrm'] = m
        if reg == 7:
            raise DecodeError('bad ff extension')
        if reg in (3, 5):
            if isinstance(rm, Reg):
                raise DecodeError('far indirect with register operand')
            rm = Mem(rm.base, rm.disp, rm.disp_size, 4, rm.seg)
        name = ('inc', 'dec', 'call', 'call', 'jmp', 'jmp', 'push')[reg]
        flow = {2: 'callind', 3: 'callind', 4: 'jmpind', 5: 'jmpind'}.get(reg, 'next')
        set_(name, rm, flow=flow)
        if reg in (3, 5):
            ins['note'] = 'far'
        return done()
    raise DecodeError('unsupported opcode %02x' % op)


# ---------------------------------------------------------------- NASM output

NASM_PRELUDE = """\
; Helper macros for encodings NASM would otherwise choose differently.
; rr OP, dst, src  -- register/register form with the direction bit set
;                     (opcode 02/03, 0a/0b, ... 8a/8b, ModR/M mod=11).
%define RR_add 0x02
%define RR_or  0x0a
%define RR_adc 0x12
%define RR_sbb 0x1a
%define RR_and 0x22
%define RR_sub 0x2a
%define RR_xor 0x32
%define RR_cmp 0x3a
%define RR_mov 0x8a
%define RN_al 0
%define RN_cl 1
%define RN_dl 2
%define RN_bl 3
%define RN_ah 4
%define RN_ch 5
%define RN_dh 6
%define RN_bh 7
%define RN_ax 0
%define RN_cx 1
%define RN_dx 2
%define RN_bx 3
%define RN_sp 4
%define RN_bp 5
%define RN_si 6
%define RN_di 7
%define RW_al 0
%define RW_cl 0
%define RW_dl 0
%define RW_bl 0
%define RW_ah 0
%define RW_ch 0
%define RW_dh 0
%define RW_bh 0
%define RW_ax 1
%define RW_cx 1
%define RW_dx 1
%define RW_bx 1
%define RW_sp 1
%define RW_bp 1
%define RW_si 1
%define RW_di 1
%macro rr 3
    db RR_%1 + RW_%2, 0xc0 | (RN_%2 << 3) | RN_%3
%endmacro
"""


def hx(v):
    return '0x%x' % v if v >= 0 else '-0x%x' % -v


def _size_kw(size):
    return {1: 'byte', 2: 'word', 4: 'far'}.get(size)


def format_mem(m, sized=True, far_kw=False):
    inner = ''
    if m.base is None:
        inner = hx(m.disp)
    else:
        inner = m.base
        if m.disp_size == 1:
            inner = 'byte %s%s%s' % (m.base, '+' if m.disp >= 0 else '-', hx(abs(m.disp)))
        elif m.disp_size == 2:
            inner = 'word %s%s%s' % (m.base, '+' if m.disp >= 0 else '-', hx(abs(m.disp)))
    if m.seg:
        inner = '%s:%s' % (m.seg, inner)
    text = '[%s]' % inner
    if sized and m.size in (1, 2):
        text = '%s %s' % (_size_kw(m.size), text)
    return text


class Unformattable(Exception):
    pass


def format_nasm(ins, label_for=None):
    """Return NASM text expected to reassemble to ``ins.raw``.

    ``label_for(offset)`` returns a label for near targets, or None to emit
    ``$+n``-style numeric targets. Raises Unformattable when the caller
    should emit ``db``.
    """
    label_for = label_for or (lambda off: None)
    if ins.note in ('x87', 'fp-emulator', 'alias-82', 'sal-6', 'test-1'):
        raise Unformattable(ins.note)
    if any(p == 0xf0 for p in ins.prefixes):
        raise Unformattable('lock')
    ops = ins.operands
    mn = ins.mnemonic
    has_mem = any(isinstance(o, Mem) for o in ops)
    seg_prefixes = [p for p in ins.prefixes if p in PREFIX_SEG]
    rep_prefixes = [p for p in ins.prefixes if p in (0xf2, 0xf3)]
    if len(seg_prefixes) > 1 or len(rep_prefixes) > 1:
        raise Unformattable('multiple prefixes')
    string = ins.opcode in STRING_OPS
    if rep_prefixes and not string:
        raise Unformattable('rep on non-string')
    if seg_prefixes and not has_mem and not string:
        raise Unformattable('segment prefix without memory operand')
    if string:
        text = mn
        if seg_prefixes:
            text = '%s %s' % (PREFIX_SEG[seg_prefixes[0]], text)
        if rep_prefixes:
            if ins.opcode in (0xa6, 0xa7, 0xae, 0xaf):
                r = 'repe' if rep_prefixes[0] == 0xf3 else 'repne'
            else:
                r = 'rep' if rep_prefixes[0] == 0xf3 else 'repne'
            text = '%s %s' % (r, text)
        return text
    if seg_prefixes:
        # NASM drops nothing, but it emits the override even when it equals
        # the default; that is what we want. Prefix must precede opcode
        # immediately, which is the only order NASM produces.
        if ins.prefixes[-1] not in PREFIX_SEG:
            raise Unformattable('prefix order')

    if ins.flow in ('jcc',):
        lbl = label_for(ops[0].target)
        if lbl is None:
            raise Unformattable('no label for target')
        return '%s %s' % (mn, lbl)
    if ins.flow in ('jmp', 'call'):
        r = ops[0]
        lbl = label_for(r.target)
        if lbl is None:
            raise Unformattable('no label for target')
        if mn == 'call':
            return 'call %s' % lbl
        return 'jmp %s %s' % ('short' if r.size == 1 else 'near', lbl)
    if isinstance(ops[0] if ops else None, Far):
        return '%s %s:%s' % (mn, hx(ops[0].seg), hx(ops[0].off))
    if ins.flow in ('jmpind', 'callind'):
        m = ops[0]
        if isinstance(m, Reg):
            return '%s %s' % (mn, m.name)
        if ins.note == 'far':
            return '%s far %s' % (mn, format_mem(m, sized=False))
        return '%s word %s' % (mn, format_mem(m, sized=False))
    if mn in ('ret', 'retf') and ops:
        return '%s %s' % (mn, hx(ops[0].value))
    if mn == 'int':
        return 'int %s' % hx(ops[0].value)
    if mn == 'enter':
        return 'enter %s, %s' % (hx(ops[0].value), hx(ops[1].value))
    if mn in ('aam', 'aad'):
        if ops[0].value != 10:
            raise Unformattable('aam/aad base')
        return mn
    if not ops:
        return mn

    # Encodings where NASM would pick a different opcode.
    op = ins.opcode
    if mn == 'xchg':
        raise Unformattable('xchg operand order')
    if mn == 'test' and all(isinstance(o, Reg) for o in ops):
        pass  # 84/85 Ev,Gv: NASM encodes "test rm, reg" identically
    if (op < 0x40 and (op & 7) in (2, 3) or op in (0x8a, 0x8b)) and isinstance(ops[1], Reg):
        return 'rr %s, %s, %s' % (mn, ops[0].name, ops[1].name)
    if op == 0x81 and isinstance(ops[0], Reg) and ops[0].name == 'ax':
        raise Unformattable('81 with ax')
    if op == 0x80 and isinstance(ops[0], Reg) and ops[0].name == 'al':
        raise Unformattable('80 with al')
    if op in (0xc6, 0xc7) and isinstance(ops[0], Reg):
        raise Unformattable('c6/c7 with register')
    if op == 0xf7 and isinstance(ops[0], Reg) and ops[0].name == 'ax' and mn == 'test':
        raise Unformattable('f7 test ax')
    if op == 0xf6 and isinstance(ops[0], Reg) and ops[0].name == 'al' and mn == 'test':
        raise Unformattable('f6 test al')
    if op == 0xff and mn == 'push' and isinstance(ops[0], Reg):
        raise Unformattable('ff push reg')
    if op in (0xfe, 0xff) and mn in ('inc', 'dec') and isinstance(ops[0], Reg) and ops[0].size == 2:
        raise Unformattable('ff inc/dec reg')
    if op == 0x8f and isinstance(ops[0], Reg):
        raise Unformattable('8f pop reg')
    if op in (0xc1, 0xc0) and ops[1].value == 1:
        raise Unformattable('shift imm 1')

    parts = []
    reg_sized = any(isinstance(o, Reg) for o in ops)
    for i, o in enumerate(ops):
        if isinstance(o, Reg):
            parts.append(o.name)
        elif isinstance(o, Mem):
            need = not reg_sized and o.size in (1, 2)
            if mn in ('les', 'lds', 'lea', 'bound'):
                need = False
            parts.append(format_mem(o, sized=need))
        elif isinstance(o, Imm):
            parts.append(_format_imm(op, o))
    return '%s %s' % (mn, ', '.join(parts))


def _format_imm(op, o):
    if o.enc == 0:
        return '1'
    if op in (0x83, 0x6b, 0x6a):
        return 'byte %s' % hx(o.signed)
    if o.enc == 2 and not 0xb8 <= op <= 0xbf:
        # 81, 05/0d/..., 69, 68, c7, a9: keep the 16-bit immediate even
        # when a sign-extended imm8 would fit.
        return 'strict word %s' % hx(o.value)
    return hx(o.value)


def format_db(raw):
    return 'db ' + ', '.join('0x%02x' % b for b in raw)


def format_text(ins):
    """Human-readable (not necessarily reassemblable) text for comments."""
    ops = []
    for o in ins.operands:
        if isinstance(o, Reg):
            ops.append(o.name)
        elif isinstance(o, Mem):
            ops.append(format_mem(o).replace('byte b', 'b').replace('word b', 'b')
                       .replace('[byte ', '[').replace('[word ', '['))
        elif isinstance(o, Imm):
            ops.append(hx(o.value))
        elif isinstance(o, Far):
            ops.append('%04x:%04x' % (o.seg, o.off))
        elif isinstance(o, Rel):
            ops.append('0x%x' % o.target)
    pre = []
    if ins.rep:
        pre.append(ins.note if ins.note in ('repe', 'repne') else ins.rep)
    if ins.note == 'far':
        ops[0] = 'far ' + ops[0]
    return ' '.join(pre + [ins.mnemonic, ', '.join(ops)]).strip()
