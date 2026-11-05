/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_rtl_graphics.h"

/* GATE.RTL:0x12773..0x12821. Both signed IMUL and the SAR/XOR/SUB
 * rounding idiom operate on the LOW 16 product bits. The latter is
 * truncation toward zero, including for negative products. Do not promote
 * the multiply to an unconstrained host int calculation. */
static uint8_t scaled_byte(uint8_t sample, uint16_t factor)
{
    uint16_t product = (uint16_t)((uint16_t)(int16_t)(int8_t)sample * factor);
    return (uint8_t)((int16_t)product / 64);
}

void gate_rtl_12773(void)
{
    uint8_t palette[0x300];
    uint16_t factor;
    int16_t count;

    if (gate_rtl_graphics_mode() == 0)
        return;
    gate_rtl_graphics_set_index(0);
    if (gate_rtl_graphics_mode() == 1) {
        gate_rtl_graphics_palette_upload(0);
        return;
    }

    count = gate_rtl_graphics_mode() == 3 ? 0x300 : 0x30;
    for (factor = 0; factor <= 64; factor = (uint16_t)(factor + 4u)) {
        int16_t i;
        for (i = 0; i < count; ++i)
            palette[i] = scaled_byte(gate_rtl_graphics_byte((uint16_t)i), factor);
        gate_rtl_graphics_palette_upload(palette);
    }
}

/* GATE.RTL:0x12823..0x12891. The original REPNE STOSB simply clears
 * the positive signed count from the mode-indexed table, then invokes the
 * palette service with a zero FAR pointer. */
void gate_rtl_12823(void)
{
    uint16_t mode = gate_rtl_graphics_mode();
    if (mode != 0) {
        uint16_t index = (uint16_t)(mode << 1);
        if (gate_rtl_graphics_byte_count(index) > 0) {
            int16_t count = gate_rtl_graphics_byte_count(index);
            uint16_t i;
            for (i = 0; i < (uint16_t)count; ++i)
                gate_rtl_graphics_set_byte(i, 0);
        }
        gate_rtl_graphics_palette_upload(0);
    }
}

/* GATE.RTL:0x12893..0x12995. Mode 1 performs three destructive 16-byte
 * bit transformations of the *source buffer*. Other nonzero modes fade
 * the original signed samples from factor 64 to 0 inclusive. Both paths
 * finish by calling the independently recovered 0x12823 routine. */
void gate_rtl_12893(void)
{
    uint8_t palette[0x308];
    uint16_t mode = gate_rtl_graphics_mode();

    if (mode == 0)
        return;
    gate_rtl_graphics_set_index(0);
    if (mode == 1) {
        int16_t passes;
        for (passes = 3; passes > 0; --passes) {
            uint16_t i;
            for (i = 0; i < 16; ++i) {
                uint8_t sample = gate_rtl_graphics_byte(i);
                int16_t shifted = (int16_t)((int16_t)(int8_t)sample >> 3);
                uint8_t replacement = (uint8_t)
                    (((uint8_t)(~shifted) & sample & 7u) << 3 |
                     (sample & (uint8_t)shifted & 7u));
                gate_rtl_graphics_set_byte(i, replacement);
            }
            gate_rtl_graphics_palette_upload(0);
            gate_rtl_graphics_delay(100);
        }
    } else {
        int16_t count = gate_rtl_graphics_byte_count((uint16_t)(mode << 1));
        int16_t factor;
        for (factor = 64; factor >= 0; factor = (int16_t)(factor - 4)) {
            int16_t i;
            for (i = 0; i < count; ++i)
                palette[i] = scaled_byte(gate_rtl_graphics_byte((uint16_t)i),
                                         (uint16_t)factor);
            gate_rtl_graphics_palette_upload(palette);
        }
    }
    gate_rtl_12823();
}

/* GATE.RTL:0x13185..0x13190: zero the first byte of the caller's FAR
 * work record, unconditionally (there is no null check). */
void gate_rtl_13185(GateFarPtr work)
{
    gate_far_set_byte(work.segment, work.offset, 0);
}

/* GATE.RTL:0x131cb..0x131f5. Non-null is the OR of both original FAR
 * pointer WORDs. The argument passed to the resource release service is
 * offset+8 with the SAME segment, even across 16-bit offset wrap. */
void gate_rtl_131cb(GateFarPtr work)
{
    if ((work.offset | work.segment) != 0 &&
        gate_far_byte(work.segment, work.offset) != 0) {
        GateFarPtr data = {work.segment, (uint16_t)(work.offset + 8u)};
        gate_rtl_graphics_release(data);
        gate_far_set_byte(work.segment, work.offset, 0);
    }
}
