/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "GATEMEM.H"
#include <dos.h>
#include <stdio.h>

int gate_dos_probe_interrupt_1c(uint16_t *observed);
int gate_dos_probe_pit_calibration(uint16_t *iterations_per_tick,
                                   uint16_t *observed);

int main(void)
{
    void (interrupt far *previous)(void);
    uint16_t ticks = 0u;
    uint16_t iterations = 0u;
    int status;

    previous = _dos_getvect(0x1cu);
    status = gate_dos_probe_interrupt_1c(&ticks);
    if (status != 0 || ticks < 2u || _dos_getvect(0x1cu) != previous) {
        printf("INT 1Ch probe: FAIL status=%d ticks=%u\n", status, ticks);
        return 1;
    }
    printf("INT 1Ch probe: PASS ticks=%u, prior vector restored\n", ticks);
    ticks = 0u;
    status = gate_dos_probe_pit_calibration(&iterations, &ticks);
    if (status != 0 || ticks < 8u || iterations == 0u ||
        _dos_getvect(0x1cu) != previous) {
        printf("PIT 60Hz probe: FAIL status=%d ticks=%u loops=%u\n",
               status, ticks, iterations);
        return 1;
    }
    printf("PIT 60Hz probe: PASS ticks=%u loops_per_tick=%u, "
           "prior vector restored, PIT reset to FFFFh, "
           "DOS date/time restored\n", ticks, iterations);
    return 0;
}
