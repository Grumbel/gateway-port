/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_rtl_image.h"

/* Linked Microsoft-style C runtime srand at 12a5:3afa (physical 1654a
 * in the 22-second original intro RAM). The argument is a single 16-bit
 * WORD: high half of the retained 32-bit state is cleared. */
void gate_far_12a5_3afa(uint16_t seed)
{
    gate_ds_set_word(0x358e, seed);
    gate_ds_set_word(0x3590, 0);
}

/* Original FAR rand at 12a5:3b0c, physical 1655c in the same capture.
 * The 8086 multiplies a little-endian 32-bit state by 000343fdh and adds
 * 00269ec3h, retaining only the low 32 bits, then masks bit 15 of DX.
 * RTL:13115 and RTL:12f6b consume this exact 0..32767 return value for
 * variable intro animation frame delays. */
uint16_t gate_far_12a5_3b0c(void)
{
    uint32_t state = (uint32_t)gate_ds_word(0x358e) |
                     ((uint32_t)gate_ds_word(0x3590) << 16);
    state = state * 0x000343fdu + 0x00269ec3u;
    gate_ds_set_word(0x358e, (uint16_t)state);
    gate_ds_set_word(0x3590, (uint16_t)(state >> 16));
    return (uint16_t)((state >> 16) & 0x7fffu);
}
