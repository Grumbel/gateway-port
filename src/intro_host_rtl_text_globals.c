/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "gate_rtl.h"

/* These linked DS offsets are observed in 0c4d:0604, 061b, 062c and
 * 0653 (the relocated ebf7..edff original C text renderer bank). No
 * fixed DOS segments or fonts are embedded in the native adapter. */
GateFarPtr gate_rtl_active_font(void)
{
    return (GateFarPtr){gate_ds_word(0x54cc), gate_ds_word(0x54ca)};
}

int16_t gate_rtl_cursor_x(void)
{
    return (int16_t)gate_ds_word(0x550a);
}

int16_t gate_rtl_cursor_y(void)
{
    return (int16_t)gate_ds_word(0x550c);
}

void gate_rtl_set_cursor(int16_t x, int16_t y)
{
    gate_ds_set_word(0x550a, (uint16_t)x);
    gate_ds_set_word(0x550c, (uint16_t)y);
}

uint16_t gate_rtl_ink_color(void)
{
    return gate_ds_word(0x54c8);
}

int16_t gate_rtl_background_color(void)
{
    return (int16_t)gate_ds_word(0x54c6);
}

void gate_rtl_set_colors(uint16_t ink, int16_t background)
{
    gate_ds_set_word(0x54c8, ink);
    gate_ds_set_word(0x54c6, (uint16_t)background);
}
