/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_rtl_palette.h"

/* GATE.RTL:0xf473..0xf49a. Poll until retrace bit 3 first CLEARS,
 * then SETS. Both loops deliberately have no timeout or backoff. */
void gate_rtl_f473(void)
{
    while ((gate_rtl_palette_in(gate_rtl_palette_status_port()) & 8u) != 0)
        ;
    while ((gate_rtl_palette_in(gate_rtl_palette_status_port()) & 8u) == 0)
        ;
}

static uint8_t palette_byte(GateFarPtr pointer)
{
    return gate_far_byte(pointer.segment, pointer.offset);
}

static void next_byte(GateFarPtr *pointer)
{
    pointer->offset = (uint16_t)(pointer->offset + 1u);
}

/* GATE.RTL:0xf61b..0xf826. Both source FAR pointer words are passed by
 * value, but the original C mutates its stack argument's OFFSET while
 * scanning RGB triples; the segment never carries on 0xffff wrap.
 *
 * Original compiler quirks intentionally retained:
 * - test of the first FAR byte even when pointer == 0000:0000;
 * - signed color-count comparisons and signed division by three;
 * - mode-1 packed samples are indexed by DI, without pointer increments;
 * - mode!=1 conversion reads only the first byte of each triple, divides
 *   its signed value by 31, then advances the pointer by three;
 * - the attribute-controller access at end of mode!=3 writes three bytes.
 */
void gate_rtl_f61b(GateFarPtr palette)
{
    uint16_t mode;

    if (gate_rtl_palette_mode() == 0)
        return;
    gate_rtl_f473();

    if (gate_rtl_palette_video_type() == 5) {
        uint16_t initial = palette_byte(palette) != 0 ? 15u : 0u;
        int16_t count;
        int16_t color;

        mode = gate_rtl_palette_mode();
        count = gate_rtl_palette_color_count((uint16_t)(4u + (mode << 1)));
        if (gate_rtl_palette_mode() != 1)
            count = (int16_t)(count / 3);

        gate_rtl_palette_out(0x3c8, 0);
        for (color = 0; color < count; ++color) {
            if (gate_rtl_palette_conversion() == 0) {
                if (gate_rtl_palette_mode() == 1) {
                    uint8_t packed = gate_far_byte(
                        palette.segment,
                        (uint16_t)(palette.offset + (uint16_t)color));
                    /* Each pair of packed color bits maps 0..3 to
                     * 0,21,42,63. SAR/AND/IMUL in the original. */
                    gate_rtl_palette_out(0x3c9, (uint8_t)(
                        21u * (((packed >> 1) & 2u) | ((packed >> 5) & 1u))));
                    gate_rtl_palette_out(0x3c9, (uint8_t)(
                        21u * (((packed >> 4) & 1u) | (packed & 2u))));
                    gate_rtl_palette_out(0x3c9, (uint8_t)(
                        21u * (((packed << 1) & 2u) | ((packed >> 3) & 1u))));
                } else {
                    gate_rtl_palette_out(0x3c9, palette_byte(palette));
                    next_byte(&palette);
                    gate_rtl_palette_out(0x3c9, palette_byte(palette));
                    next_byte(&palette);
                    gate_rtl_palette_out(0x3c9, palette_byte(palette));
                    next_byte(&palette);
                }
            } else {
                int16_t table_index;
                if (gate_rtl_palette_mode() == 1) {
                    uint8_t bits = gate_far_byte(
                        palette.segment,
                        (uint16_t)(palette.offset + (uint16_t)color));
                    table_index = (int16_t)(
                        ((bits & 1u) ? 2 : 0) +
                        ((bits & 2u) ? 2 : 0) +
                        ((bits & 4u) ? 2 : 0) +
                        ((bits & 8u) ? 1 : 0) +
                        ((bits & 16u) ? 1 : 0) +
                        ((bits & 32u) ? 1 : 0));
                } else {
                    int8_t sample = (int8_t)palette_byte(palette);
                    table_index = (int16_t)((sample / 31) * 3);
                    palette.offset = (uint16_t)(palette.offset + 3u);
                }
                gate_rtl_palette_out(0x3c9,
                    gate_rtl_palette_table_byte(
                        (uint16_t)(0x35cu + (uint16_t)table_index)));
                gate_rtl_palette_out(0x3c9,
                    gate_rtl_palette_table_byte(
                        (uint16_t)(0x366u + (uint16_t)table_index)));
                gate_rtl_palette_out(0x3c9, 0);
            }
        }

        if (gate_rtl_palette_mode() != 3) {
            (void)gate_rtl_palette_in(gate_rtl_palette_status_port());
            gate_rtl_palette_out(0x3c0, 0x11);
            gate_rtl_palette_out(0x3c0, (uint8_t)initial);
            gate_rtl_palette_out(0x3c0, 0x20);
        }
    } else {
        uint16_t i;
        (void)gate_rtl_palette_in(gate_rtl_palette_status_port());
        for (i = 0; i < 16; ++i) {
            gate_rtl_palette_out(0x3c0, (uint8_t)i);
            gate_rtl_palette_out(0x3c0,
                gate_far_byte(palette.segment,
                              (uint16_t)(palette.offset + i)));
        }
        (void)gate_rtl_palette_in(gate_rtl_palette_status_port());
        gate_rtl_palette_out(0x3c0, 0x11);
        gate_rtl_palette_out(0x3c0, 0);
        gate_rtl_palette_out(0x3c0, 0x20);
    }
}
