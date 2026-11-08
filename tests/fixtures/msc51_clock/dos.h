/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Host-only deterministic INT 21h stand-in for CLOCK.C regression tests. */
#ifndef GATE_CLOCK_FIXTURE_DOS_H
#define GATE_CLOCK_FIXTURE_DOS_H

#include <stdint.h>

union REGS {
    struct {
        uint16_t ax, bx, cx, dx, si, di, cflag, flags;
    } x;
    struct {
        uint8_t al, ah, bl, bh, cl, ch, dl, dh;
        uint8_t sil, sih, dil, dih, flagl, flagh, xflagl, xflagh;
    } h;
};

int int86(int interrupt, union REGS *input, union REGS *output);

#endif
