/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* DOS-side checks of the original RTL:5806..58fd timer boundary.
 * The normal-IRQ test leaves the PIT untouched. A second, explicit
 * DOSBox-only test installs the same interrupt handler, programs PIT
 * channel 0 to the original 4DAEh divisor, measures eight callbacks,
 * and restores both the prior vector and the original RTL's observed
 * FFFFh restore divisor. No production launcher function calls these
 * probes, and the sampled C loop is not an instruction-identical port
 * of the original signed-IMUL calibration body. */
#include "GATEMEM.H"
#include <dos.h>
#include <conio.h>

#define GATE_TIMER_VECTOR 0x1cu
#define GATE_TIMER_DAY_MS 86400000ul
#define GATE_TIMER_TIMEOUT_MS 2000ul
#define GATE_TIMER_SPIN_LIMIT 2000000ul
#define GATE_TIMER_PIT_COMMAND 0x43u
#define GATE_TIMER_PIT_COUNTER 0x40u
#define GATE_TIMER_PIT_MODE 0x34u
#define GATE_TIMER_CALIBRATION_DIVISOR 0x4daeu
#define GATE_TIMER_RESTORE_DIVISOR 0xffffu

static volatile uint16_t gate_timer_callback_count;
static volatile uint8_t gate_timer_remaining;

static void interrupt far gate_timer_callback(void)
{
    if (gate_timer_callback_count != 0xffffu)
        ++gate_timer_callback_count;
    if (gate_timer_remaining != 0u)
        --gate_timer_remaining;
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
    gate_timer_remaining = 2u;
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

/* The original RTL emits two delay JMPs between PIT writes. outp() and
 * its C call overhead are deliberately not cycle-for-cycle equivalent.
 * Reprogramming a PC's PIT is safe only in an isolated DOSBox guest;
 * unlike the non-mutating 18Hz check above, this test is never entered
 * by the recovered launcher or any default host runtime. */
static void gate_timer_program_pit(uint16_t divisor)
{
    outp(GATE_TIMER_PIT_COMMAND, GATE_TIMER_PIT_MODE);
    outp(GATE_TIMER_PIT_COUNTER, (int)(divisor & 255u));
    outp(GATE_TIMER_PIT_COUNTER, (int)(divisor >> 8));
}

/* Out parameter is a C-loop count / eight, NOT the original RTL:5836
 * 16-bit result. Returns the same status codes as the INT 1Ch test.
 * As with original RTL:5840..58e1, the restored PIT divisor is FFFFh;
 * the original may have recorded it separately before this routine.
 * Both interrupt vector and PIT are restored on timeout, too. */
int gate_dos_probe_pit_calibration(uint16_t *iterations_per_tick,
                                   uint16_t *observed)
{
    void (interrupt far *previous)(void);
    uint32_t start, now, loops;
    uint16_t count;

    if (iterations_per_tick == 0 || observed == 0)
        return -1;
    *iterations_per_tick = 0u;
    *observed = 0u;
    previous = _dos_getvect(GATE_TIMER_VECTOR);
    if (previous == 0)
        return -2;

    gate_timer_callback_count = 0u;
    gate_timer_remaining = 8u;
    start = gate_timer_dos_milliseconds();
    _dos_setvect(GATE_TIMER_VECTOR, gate_timer_callback);
    gate_timer_program_pit(GATE_TIMER_CALIBRATION_DIVISOR);

    for (loops = 0ul; loops < GATE_TIMER_SPIN_LIMIT; ++loops) {
        if (gate_timer_remaining == 0u)
            break;
        if ((loops & 1023ul) == 0ul) {
            now = gate_timer_dos_milliseconds();
            if (now < start)
                now += GATE_TIMER_DAY_MS;
            if (now - start >= GATE_TIMER_TIMEOUT_MS)
                break;
        }
    }

    /* Always remove our callback before restoring the BIOS-rate timer.
     * The original code performs the equivalent operations under CLI.
     * These DOSBox probe calls are not a hardware-safe CLI/IVT clone. */
    _dos_setvect(GATE_TIMER_VECTOR, previous);
    gate_timer_program_pit(GATE_TIMER_RESTORE_DIVISOR);
    count = gate_timer_callback_count;
    *observed = count;
    *iterations_per_tick = (uint16_t)(loops >> 3);
    if (_dos_getvect(GATE_TIMER_VECTOR) != previous)
        return -3;
    return count >= 8u && *iterations_per_tick != 0u ? 0 : -1;
}
