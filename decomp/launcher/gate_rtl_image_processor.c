/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_rtl_image.h"
#include "gate_rtl_graphics.h"

/* GATE.RTL:0x12ed7..0x12f2a. The original image draw wrapper passes a
 * 136-byte work record's payload (+8) to a lower-level FAR drawing service.
 * The two image-origin WORDs are original relocated DS:1418/141a scalars;
 * they are not a C++ viewport transform. The display mode is a signed
 * BYTE, not an unsigned WORD. The renderer consumes twelve stack bytes. */
void gate_rtl_12ed7(GateFarPtr work)
{
    uint16_t saved;
    uint16_t mode;
    uint16_t x;
    uint16_t y;
    GateFarPtr image;

    if (gate_far_byte(work.segment, work.offset) == 0)
        return;

    saved = gate_far_1e48_04d9();
    image = work;
    image.offset = (uint16_t)(image.offset + 8u);
    mode = (uint16_t)(int16_t)(int8_t)gate_far_byte(
        work.segment, (uint16_t)(work.offset + 1u));
    x = (uint16_t)(gate_far_word(work.segment,
        (uint16_t)(work.offset + 2u)) + gate_rtl_resource_ds_word(2));
    y = (uint16_t)(gate_far_word(work.segment,
        (uint16_t)(work.offset + 4u)) + gate_rtl_resource_ds_word(4));
    gate_far_24a7_000c(0, y, x, mode, image);
    gate_far_1e48_03c5(saved);
}

/* GATE.RTL:0x12f2b..0x12f6a. A caller-supplied sequence of 0x88-byte
 * records, traversed by offset-only FAR arithmetic. Negative/zero counts
 * are ignored, rather than reinterpreted as large unsigned loops. */
void gate_rtl_12f2b(GateFarPtr work, int16_t count)
{
    uint16_t i;
    if (count <= 0)
        return;
    for (i = 0; i < (uint16_t)count; ++i) {
        gate_rtl_12ed7(work);
        work.offset = (uint16_t)(work.offset + 0x88u);
    }
}

/* GATE.RTL:0x13115..0x13184. Draw each loaded frame, then use the
 * original clock's LOW word to calculate its delay from bytes +6/+7.
 * The x86 8-bit MUL and 16-bit DIV yield:
 * 50*byte6 + (clock_low % (50*byte7 + 1)).
 * The delay service gets the zero-extended 32-bit result. */
void gate_rtl_13115(GateFarPtr work, int16_t count)
{
    uint16_t i;
    if (count <= 0)
        return;
    for (i = 0; i < (uint16_t)count; ++i) {
        uint16_t clock_low;
        uint16_t divisor;
        uint16_t delay;
        gate_rtl_12ed7(work);
        clock_low = gate_far_12a5_3b0c();
        divisor = (uint16_t)(50u * (uint16_t)gate_far_byte(
            work.segment, (uint16_t)(work.offset + 7u)) + 1u);
        delay = (uint16_t)((clock_low % divisor) + 50u *
            (uint16_t)gate_far_byte(work.segment,
            (uint16_t)(work.offset + 6u)));
        gate_far_223f_00b6((uint32_t)delay);
        work.offset = (uint16_t)(work.offset + 0x88u);
    }
}

/* GATE.RTL:0x13191..0x131c8. Clear just the active first BYTE of each
 * 136-byte work record. The caller owns any resource release. */
void gate_rtl_13191(GateFarPtr work, int16_t count)
{
    uint16_t i;
    if (count <= 0)
        return;
    for (i = 0; i < (uint16_t)count; ++i) {
        gate_far_set_byte(work.segment, work.offset, 0);
        work.offset = (uint16_t)(work.offset + 0x88u);
    }
}

/* GATE.RTL:0x131f7..0x1323e. Unlike the initializer, this release loop
 * checks the complete FAR pointer (segment OR offset) before traversing. */
void gate_rtl_131f7(GateFarPtr work, int16_t count)
{
    uint16_t i;
    if ((work.segment | work.offset) == 0 || count <= 0)
        return;
    for (i = 0; i < (uint16_t)count; ++i) {
        gate_rtl_131cb(work);
        work.offset = (uint16_t)(work.offset + 0x88u);
    }
}
