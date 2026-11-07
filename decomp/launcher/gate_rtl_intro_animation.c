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
void gate_rtl_play_picture_delta_sequence(uint16_t first, uint16_t next, int16_t count)
{
    uint8_t record[0x88];
    GateFarPtr work = gate_stack_pointer(record);
    uint32_t deadline = 0;

    gate_rtl_fade_palette_out();
    gate_linked_set_intro_pen_color(gate_far_word(gate_rtl_anim_segment_22(), 0x4c));
    gate_linked_fill_intro_rectangle(2, 0, 0,
        gate_far_word(gate_rtl_anim_segment_26(), 0x3e),
        gate_far_word(gate_rtl_anim_segment_24(), 0x42));

    record[0] = 0;
    if ((int16_t)first >= 0) {
        (void)gate_rtl_load_picture_record(work, first, 0);
        gate_rtl_draw_picture_record(work);
        gate_rtl_fade_palette_in();
        gate_rtl_release_picture_record(work);
    }

    while (count > 0) {
        uint32_t now;
        uint16_t random_low;
        uint16_t divisor;
        uint16_t delay;
        --count;
        if (gate_rtl_load_picture_record(work, next, 1) == 0)
            break;
        next = (uint16_t)(next + 1u);

        for (;;) {
            now = gate_linked_animation_elapsed_milliseconds();
            if (rtl_deadline_reached(now, deadline))
                break;
            if (gate_linked_poll_animation_input() == 0)
                goto cleanup;
        }

        gate_rtl_prepare_picture_display();
        gate_rtl_draw_picture_record(work);
        gate_rtl_release_picture_record(work);
        now = gate_linked_animation_elapsed_milliseconds();
        random_low = gate_linked_runtime_random();
        divisor = (uint16_t)(50u * (uint16_t)gate_far_byte(work.segment,
            (uint16_t)(work.offset + 7u)) + 1u);
        delay = (uint16_t)((random_low % divisor) + 50u *
            (uint16_t)gate_far_byte(work.segment,
            (uint16_t)(work.offset + 6u)));
        deadline = (uint32_t)(now + delay);
    }

cleanup:
    gate_rtl_release_picture_record(work);
}
