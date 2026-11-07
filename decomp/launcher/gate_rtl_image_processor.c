/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_rtl_image.h"
#include "gate_rtl_graphics.h"

/* GATE.RTL:0x12ed7..0x12f2a. The original image draw wrapper passes a
 * 136-byte work record's payload (+8) to a lower-level FAR drawing service.
 * The two image-origin WORDs are original relocated DS:1418/141a scalars;
 * they are not a C++ viewport transform. The display mode is a signed
 * BYTE, not an unsigned WORD. The renderer consumes twelve stack bytes. */
void gate_rtl_draw_picture_record(GateFarPtr work)
{
    uint16_t saved;
    uint16_t mode;
    uint16_t x;
    uint16_t y;
    GateFarPtr image;

    if (gate_far_byte(work.segment, work.offset) == 0)
        return;

    saved = gate_linked_save_vfx_draw_state();
    image = work;
    image.offset = (uint16_t)(image.offset + GATE_PICTURE_RECORD_VFX_IMAGE);
    mode = (uint16_t)(int16_t)(int8_t)gate_far_byte(
        work.segment, (uint16_t)(work.offset + GATE_PICTURE_RECORD_BLIT_MODE));
    x = (uint16_t)(gate_far_word(work.segment,
        (uint16_t)(work.offset + GATE_PICTURE_RECORD_X)) + gate_rtl_resource_ds_word(GATE_RESOURCE_DRAW_ORIGIN_X));
    y = (uint16_t)(gate_far_word(work.segment,
        (uint16_t)(work.offset + GATE_PICTURE_RECORD_Y)) + gate_rtl_resource_ds_word(GATE_RESOURCE_DRAW_ORIGIN_Y));
    gate_linked_blit_vfx_picture(0, y, x, mode, image);
    gate_linked_restore_vfx_draw_state(saved);
}

/* GATE.RTL:0x12f2b..0x12f6a. A caller-supplied sequence of 0x88-byte
 * records, traversed by offset-only FAR arithmetic. Negative/zero counts
 * are ignored, rather than reinterpreted as large unsigned loops. */
void gate_rtl_draw_picture_record_sequence(GateFarPtr work, int16_t count)
{
    uint16_t i;
    if (count <= 0)
        return;
    for (i = 0; i < (uint16_t)count; ++i) {
        gate_rtl_draw_picture_record(work);
        work.offset = (uint16_t)(work.offset + GATE_PICTURE_RECORD_BYTES);
    }
}

/* GATE.RTL:0x13115..0x13184. Draw each loaded frame, then use the
 * original clock's LOW word to calculate its delay from bytes +6/+7.
 * The x86 8-bit MUL and 16-bit DIV yield:
 * 50*byte6 + (clock_low % (50*byte7 + 1)).
 * The delay service gets the zero-extended 32-bit result. */
void gate_rtl_play_timed_picture_records(GateFarPtr work, int16_t count)
{
    uint16_t i;
    if (count <= 0)
        return;
    for (i = 0; i < (uint16_t)count; ++i) {
        uint16_t clock_low;
        uint16_t divisor;
        uint16_t delay;
        gate_rtl_draw_picture_record(work);
        clock_low = gate_linked_runtime_random();
        divisor = (uint16_t)(GATE_PICTURE_ANIMATION_DELAY_QUANTUM_MS * (uint16_t)gate_far_byte(
            work.segment, (uint16_t)(work.offset + GATE_PICTURE_RECORD_DELAY_SPREAD)) + 1u);
        delay = (uint16_t)((clock_low % divisor) + GATE_PICTURE_ANIMATION_DELAY_QUANTUM_MS *
            (uint16_t)gate_far_byte(work.segment,
            (uint16_t)(work.offset + GATE_PICTURE_RECORD_DELAY_BASE)));
        gate_linked_wait_animation_delay((uint32_t)delay);
        work.offset = (uint16_t)(work.offset + GATE_PICTURE_RECORD_BYTES);
    }
}

/* GATE.RTL:0x13191..0x131c8. Clear just the active first BYTE of each
 * 136-byte work record. The caller owns any resource release. */
void gate_rtl_initialize_picture_record_array(GateFarPtr work, int16_t count)
{
    uint16_t i;
    if (count <= 0)
        return;
    for (i = 0; i < (uint16_t)count; ++i) {
        gate_far_set_byte(work.segment, work.offset, 0);
        work.offset = (uint16_t)(work.offset + GATE_PICTURE_RECORD_BYTES);
    }
}

/* GATE.RTL:0x131f7..0x1323e. Unlike the initializer, this release loop
 * checks the complete FAR pointer (segment OR offset) before traversing. */
void gate_rtl_release_picture_record_array(GateFarPtr work, int16_t count)
{
    uint16_t i;
    if ((work.segment | work.offset) == 0 || count <= 0)
        return;
    for (i = 0; i < (uint16_t)count; ++i) {
        gate_rtl_release_picture_record(work);
        work.offset = (uint16_t)(work.offset + GATE_PICTURE_RECORD_BYTES);
    }
}
