/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_rtl_image.h"
#include "gate_rtl_graphics.h"

/* The original compiler compares the upper clock WORD as signed and,
 * only when equal, compares the lower WORD as unsigned. This differs
 * from a plain uint32_t >= when bit 31 changes. */
static uint16_t rtl_deadline_reached(uint32_t now, uint32_t deadline)
{
    int16_t now_high = (int16_t)(now >> 16);
    int16_t deadline_high = (int16_t)(deadline >> 16);
    if (now_high != deadline_high)
        return now_high > deadline_high;
    return (uint16_t)now >= (uint16_t)deadline;
}

/* GATE.RTL:0x12f6b..0x130aa, original three-argument intro frame sequence.
 * The 0x90-byte stack frame contains a 0x88-byte resource work record and
 * a 32-bit deadline. First image ID is signed: negative skips the splash;
 * subsequent images load in mode 1 and advance through consecutive IDs.
 * Its peripheral routines and original RTLink globals remain explicit. */
void gate_rtl_12f6b(uint16_t first, uint16_t next, int16_t count)
{
    uint8_t record[0x88];
    GateFarPtr work = gate_stack_pointer(record);
    uint32_t deadline = 0;

    gate_rtl_12893();
    gate_far_0cdc_0aac(gate_far_word(gate_rtl_anim_segment_22(), 0x4c));
    gate_far_0cdc_0aee(2, 0, 0,
        gate_far_word(gate_rtl_anim_segment_26(), 0x3e),
        gate_far_word(gate_rtl_anim_segment_24(), 0x42));

    record[0] = 0;
    if ((int16_t)first >= 0) {
        (void)gate_rtl_12c33(work, first, 0);
        gate_rtl_12ed7(work);
        gate_rtl_12773();
        gate_rtl_131cb(work);
    }

    while (count > 0) {
        uint32_t now;
        uint16_t random_low;
        uint16_t divisor;
        uint16_t delay;
        --count;
        if (gate_rtl_12c33(work, next, 1) == 0)
            break;
        next = (uint16_t)(next + 1u);

        for (;;) {
            now = gate_far_12a5_38c4();
            if (rtl_deadline_reached(now, deadline))
                break;
            if (gate_far_1000_0008() == 0)
                goto cleanup;
        }

        gate_rtl_1266f();
        gate_rtl_12ed7(work);
        gate_rtl_131cb(work);
        now = gate_far_12a5_38c4();
        random_low = gate_far_12a5_3b0c();
        divisor = (uint16_t)(50u * (uint16_t)gate_far_byte(work.segment,
            (uint16_t)(work.offset + 7u)) + 1u);
        delay = (uint16_t)((random_low % divisor) + 50u *
            (uint16_t)gate_far_byte(work.segment,
            (uint16_t)(work.offset + 6u)));
        deadline = (uint32_t)(now + delay);
    }

cleanup:
    gate_rtl_131cb(work);
}
