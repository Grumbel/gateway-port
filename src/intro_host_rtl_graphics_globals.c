/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "gate_rtl_graphics.h"
#include "gate_rtl_image.h"

/* Relocated globals read by original GATE.RTL:12773,12823,12893.
 * Live resident code verifies DS:4dce mode, DS:4dd4 state, DS:4dda
 * mode-size table and DS:4ddc image palette. The index is BYTE-indexed
 * for palette samples but WORD-indexed for pre-shifted table offsets. */
uint16_t gate_rtl_graphics_mode(void)
{
    return gate_far_word(gate_rtl_image_mode_segment(),
                         GATE_RTL_VIDEO_MODE_OFFSET);
}

void gate_rtl_graphics_set_index(uint16_t value)
{
    gate_far_set_word(gate_rtl_preparer_status_segment(), GATE_PREPARE_STATUS_WORD, value);
}

int16_t gate_rtl_graphics_byte_count(uint16_t index)
{
    return (int16_t)gate_far_word(gate_rtl_resource_size_table_segment(),
                                  (uint16_t)(GATE_RTL_SIZE_TABLE_OFFSET + index));
}

uint8_t gate_rtl_graphics_byte(uint16_t index)
{
    return gate_far_byte(gate_rtl_image_transform_segment(),
                         (uint16_t)(GATE_RTL_TRANSFORM_OFFSET + index));
}

void gate_rtl_graphics_set_byte(uint16_t index, uint8_t value)
{
    gate_far_set_byte(gate_rtl_image_transform_segment(),
                      (uint16_t)(GATE_RTL_TRANSFORM_OFFSET + index), value);
}
