/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "GATEMEM.H"
#include <dos.h>
#include <stdio.h>

int gate_dos_probe_interrupt_1c(uint16_t *observed);

int main(void)
{
    void (interrupt far *previous)(void);
    uint16_t ticks = 0u;
    int status;

    previous = _dos_getvect(0x1cu);
    status = gate_dos_probe_interrupt_1c(&ticks);
    if (status != 0 || ticks < 2u || _dos_getvect(0x1cu) != previous) {
        printf("INT 1Ch probe: FAIL status=%d ticks=%u\n", status, ticks);
        return 1;
    }
    printf("INT 1Ch probe: PASS ticks=%u, prior vector restored\n", ticks);
    return 0;
}
