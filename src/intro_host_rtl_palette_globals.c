/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "gate_rtl_palette.h"

/* Verified relocated DS operands of GATE.RTL:f473/f61b in the captured
 * 0cdc bank. These are live guest words, not constants selected by a
 * reconstructed renderer. The mode-count table is at 0fd2 + 2*mode,
 * and table entries 035c/0366 relocate by a further 0fce bytes. */
uint16_t gate_rtl_palette_mode(void)
{
    return gate_ds_word(0x0fce);
}

uint16_t gate_rtl_palette_video_type(void)
{
    return gate_ds_word(0x5516);
}

uint16_t gate_rtl_palette_status_port(void)
{
    return gate_ds_word(0x5514);
}

int16_t gate_rtl_palette_color_count(uint16_t offset)
{
    return (int16_t)gate_ds_word((uint16_t)(0x0fce + offset));
}

uint16_t gate_rtl_palette_conversion(void)
{
    return gate_far_word(gate_ds_word(0x4daa), 0x0040);
}

uint8_t gate_rtl_palette_table_byte(uint16_t offset)
{
    return gate_far_byte(gate_ds_segment(), (uint16_t)(0x0fce + offset));
}
