/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifndef GATE_TEST_DOS_H
#define GATE_TEST_DOS_H
#define interrupt
#define far

union REGS {
    struct {
        uint16_t ax, bx, cx, dx;
    } x;
    struct {
        unsigned char al, ah, bl, bh, cl, ch, dl, dh;
    } h;
};

int int86(int number, union REGS *input, union REGS *output);
void (*_dos_getvect(unsigned int vector))(void);
void _dos_setvect(unsigned int vector, void (*handler)(void));
#endif
