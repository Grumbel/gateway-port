/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_rtl.h"

#include <stdlib.h>

/* Linked 12a5:34d8 (RTL:8bb6) fills a time sample through an SS:offset
 * pointer. Its calendar conversion is not reconstructed here. */
void gate_far_12a5_34d8(GateFarPtr destination);

/* GATE.RTL:8cc8 is the still-unrecovered timezone initializer. */
void gate_rtl_8cc8(void);

/* GATE.RTL:8cb8..8cc7, linked 12a5:35da. The original startup flag is
 * relocated to DS:6470. Repeated calls skip initialization; the one-time
 * initializer may update the same flag, so the caller increments its
 * actual post-call value rather than blindly setting it to one. */
void gate_rtl_8cb8(void)
{
    if (gate_ds_word(0x6470) == 0) {
        gate_rtl_8cc8();
        gate_ds_set_word(0x6470, (uint16_t)(gate_ds_word(0x6470) + 1u));
    }
}

void gate_far_12a5_35da(void)
{
    gate_rtl_8cb8();
}

/* GATE.RTL:929e..9339, linked 12a5:3bc0: signed 32-bit quotient.
 * The 8086 routine tests both sign words, divides magnitudes and restores
 * the sign; the returned DX:AX wraps to 32 bits. Zero divisor faults in
 * the original DIV and is never silently replaced by a result. */
uint32_t gate_rtl_929e(int32_t dividend, int32_t divisor)
{
    if (divisor == 0)
        abort(); /* original divide trap; no recovery value */
    return (uint32_t)((int64_t)dividend / (int64_t)divisor);
}

/* GATE.RTL:933a..936b, linked 12a5:3c5c. The 8086 MUL sequence returns
 * only DX:AX; carries above 32 bits are discarded. */
uint32_t gate_rtl_933a(uint32_t lhs, uint32_t rhs)
{
    return lhs * rhs;
}

/* GATE.RTL:8fa2..8ff8, linked 12a5:38c4.
 * The original resets the word at the FAR segment in DS:4e24, then reads
 * a time sample into ten stack bytes. Words 0/2 contain a 32-bit whole
 * time count and word 4 its fractional component. The global baseline is
 * DS:3584/3586, with fractional baseline DS:3588.
 *
 * Preserve the original signed 16-bit fractional subtraction (CBW/CWD)
 * and the 32-bit modular multiplication. This is source recovery only:
 * the host still supplies the elapsed clock directly for playback. */
uint32_t gate_rtl_8fa2(void)
{
    uint8_t sample[10] = {0};
    GateFarPtr destination = gate_stack_pointer(sample);
    uint32_t current;
    uint32_t baseline;
    int16_t fraction;

    gate_far_set_word(gate_ds_word(0x4e24), 0x3582, 0);
    gate_far_12a5_34d8(destination);
    current = (uint32_t)gate_far_word(destination.segment, destination.offset) |
              ((uint32_t)gate_far_word(destination.segment,
                                      (uint16_t)(destination.offset + 2)) << 16);
    baseline = (uint32_t)gate_ds_word(0x3584) |
               ((uint32_t)gate_ds_word(0x3586) << 16);
    fraction = (int16_t)(gate_far_word(destination.segment,
                                     (uint16_t)(destination.offset + 4)) -
                         gate_ds_word(0x3588));
    return gate_rtl_933a(current - baseline, 1000u) +
           (uint32_t)(int32_t)fraction;
}
