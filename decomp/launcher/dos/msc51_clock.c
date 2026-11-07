/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* DOS-time boundary for the linked EXE/RTL animation clock and wait calls.
 * Original RTL 12a5:38c4 uses a ten-byte calendar sample and DS:3584/3588
 * startup baseline. This adapter deliberately uses DOS INT 21h/2Ch's
 * hundredths-of-a-second clock until that original sampler is reconstructed.
 * It therefore cannot yet claim equivalence across midnight, DST changes,
 * or the original C runtime's startup time initialization. */
#include "STDINT.H"
#include <dos.h>

#define GATE_DOS_DAY_MILLISECONDS 86400000ul

static uint32_t gate_dos_clock_baseline;
static int gate_dos_clock_started;

/* DOS AH=2Ch: CH hours, CL minutes, DH seconds, DL hundredths. */
static uint32_t dos_time_of_day_ms(void)
{
    union REGS input, output;
    uint32_t seconds;
    input.h.ah = 0x2cu;
    int86(0x21, &input, &output);
    seconds = ((uint32_t)output.h.ch * 60ul + (uint32_t)output.h.cl) * 60ul +
              (uint32_t)output.h.dh;
    return seconds * 1000ul + (uint32_t)output.h.dl * 10ul;
}

static uint32_t gate_dos_elapsed_milliseconds(void)
{
    uint32_t now = dos_time_of_day_ms();
    if (!gate_dos_clock_started) {
        gate_dos_clock_baseline = now;
        gate_dos_clock_started = 1;
    }
    if (now >= gate_dos_clock_baseline)
        return now - gate_dos_clock_baseline;
    return GATE_DOS_DAY_MILLISECONDS - gate_dos_clock_baseline + now;
}

uint32_t gate_linked_exe_elapsed_milliseconds(void)
{
    return gate_dos_elapsed_milliseconds();
}

uint32_t gate_linked_animation_elapsed_milliseconds(void)
{
    return gate_dos_elapsed_milliseconds();
}

/* No simulated clock advance: wait until the real DOS clock progresses. */
void gate_linked_wait_animation_delay(uint32_t delay)
{
    uint32_t start = gate_dos_elapsed_milliseconds();
    while (gate_dos_elapsed_milliseconds() - start < delay) {
        /* Original animation wait may also service input/frame events. */
    }
}

void gate_rtl_graphics_delay(uint32_t delay)
{
    gate_linked_wait_animation_delay(delay);
}
