/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* First DOS-side check of the original RTL:5840..58fd INT 1Ch boundary.
 * Unlike the original timer calibration, this probe deliberately leaves
 * PIT channel 0 at its existing frequency. It verifies that MS C 5.1's
 * `interrupt far` handler can observe BIOS timer callbacks, and that the
 * previously installed vector survives both success and timeout. Only
 * CLKCHK's separate timer test calls this until the real-mode behavior
 * is confirmed on DOSBox. */
#include "GATEMEM.H"
#include <dos.h>

#define GATE_TIMER_VECTOR 0x1cu
#define GATE_TIMER_DAY_MS 86400000ul
#define GATE_TIMER_TIMEOUT_MS 2000ul
#define GATE_TIMER_SPIN_LIMIT 2000000ul

static volatile uint16_t gate_timer_callback_count;

static void interrupt far gate_timer_callback(void)
{
    if (gate_timer_callback_count != 0xffffu)
        ++gate_timer_callback_count;
}

static uint32_t gate_timer_dos_milliseconds(void)
{
    union REGS input, output;
    input.h.ah = 0x2cu;
    int86(0x21, &input, &output);
    return (((uint32_t)output.h.ch * 60ul + (uint32_t)output.h.cl) * 60ul +
            (uint32_t)output.h.dh) * 1000ul + (uint32_t)output.h.dl * 10ul;
}

/* Returns 0 on two observed ticks; -1 on timeout or missing IRQs, -2
 * for an absent previous vector, and -3 if vector restoration fails.
 * The spin limit is independent of DOS time, which may stop advancing
 * when virtual IRQ delivery is broken. The handler itself calls no DOS
 * services and never chains or touches PIT state. */
int gate_dos_probe_interrupt_1c(uint16_t *observed)
{
    void (interrupt far *previous)(void);
    uint32_t start, now, loops;
    uint16_t count;

    if (observed == 0)
        return -1;
    *observed = 0u;
    previous = _dos_getvect(GATE_TIMER_VECTOR);
    if (previous == 0)
        return -2;

    gate_timer_callback_count = 0u;
    start = gate_timer_dos_milliseconds();
    _dos_setvect(GATE_TIMER_VECTOR, gate_timer_callback);

    for (loops = 0ul; loops < GATE_TIMER_SPIN_LIMIT; ++loops) {
        if (gate_timer_callback_count >= 2u)
            break;
        if ((loops & 1023ul) == 0ul) {
            now = gate_timer_dos_milliseconds();
            if (now < start)
                now += GATE_TIMER_DAY_MS;
            if (now - start >= GATE_TIMER_TIMEOUT_MS)
                break;
        }
    }

    /* Exactly one exit from the installed-vector region, including on
     * timeout. Never return with the temporary IRQ handler installed. */
    _dos_setvect(GATE_TIMER_VECTOR, previous);
    count = gate_timer_callback_count;
    *observed = count;
    if (_dos_getvect(GATE_TIMER_VECTOR) != previous)
        return -3;
    return count >= 2u ? 0 : -1;
}
