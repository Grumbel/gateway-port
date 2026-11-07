/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "gate_rtl.h"

/* These linked DS offsets are observed in 0c4d:0604, 061b, 062c and
 * 0653 (the relocated ebf7..edff original C text renderer bank). No
 * fixed DOS segments or fonts are embedded in the native adapter. */
GateFarPtr gate_rtl_active_font(void)
{
    return (GateFarPtr){gate_ds_word(GATE_DS_ACTIVE_FONT_SEGMENT), gate_ds_word(GATE_DS_ACTIVE_FONT_OFFSET)};
}

int16_t gate_rtl_cursor_x(void)
{
    return (int16_t)gate_ds_word(GATE_DS_TEXT_CURSOR_X);
}

int16_t gate_rtl_cursor_y(void)
{
    return (int16_t)gate_ds_word(GATE_DS_TEXT_CURSOR_Y);
}

void gate_rtl_set_cursor(int16_t x, int16_t y)
{
    gate_ds_set_word(GATE_DS_TEXT_CURSOR_X, (uint16_t)x);
    gate_ds_set_word(GATE_DS_TEXT_CURSOR_Y, (uint16_t)y);
}

uint16_t gate_rtl_ink_color(void)
{
    return gate_ds_word(GATE_DS_TEXT_INK_COLOR);
}

int16_t gate_rtl_background_color(void)
{
    return (int16_t)gate_ds_word(GATE_DS_TEXT_BACKGROUND_COLOR);
}

void gate_rtl_set_colors(uint16_t ink, int16_t background)
{
    gate_ds_set_word(GATE_DS_TEXT_INK_COLOR, ink);
    gate_ds_set_word(GATE_DS_TEXT_BACKGROUND_COLOR, (uint16_t)background);
}

/* The linked RTL text routines read these mode-dependent words through
 * segment selectors in DS. Their exact relocated operands are visible in
 * the 22-second capture: ec0a/ed39/edff reference 4d82, 4d86, 4d8a and
 * 4d90, then read the original mode descriptor at offsets 3a/3c/34/3e.
 * The values change when the intro switches VGA modes; they must not be
 * cached or hardcoded to the 320x200 caption path. */
uint16_t gate_rtl_tab_advance(void)
{
    return gate_far_word(gate_ds_word(GATE_DS_FONT_TAB_ADVANCE_SEGMENT), GATE_MODE_DESC_TAB_ADVANCE);
}

int16_t gate_rtl_text_mode_pitch(void)
{
    return (int16_t)gate_far_word(gate_ds_word(GATE_DS_FONT_LINE_PITCH_SEGMENT), GATE_MODE_DESC_TEXT_LINE_PITCH);
}

int16_t gate_rtl_tab_pitch(void)
{
    return (int16_t)gate_rtl_tab_advance();
}

int16_t gate_rtl_tab_limit(void)
{
    return (int16_t)gate_far_word(gate_ds_word(GATE_DS_FONT_TAB_LIMIT_SEGMENT), GATE_MODE_DESC_SCREEN_WIDTH);
}

int16_t gate_rtl_newline_pitch(void)
{
    return (int16_t)gate_far_word(gate_ds_word(GATE_DS_FONT_HEIGHT_SEGMENT), GATE_MODE_DESC_FONT_HEIGHT);
}
