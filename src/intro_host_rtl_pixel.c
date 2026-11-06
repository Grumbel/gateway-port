/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "gate_rtl.h"
#include "gate_intro_host_memory.h"

#include <stdlib.h>

/* Native video boundary for the original glyph printer's linked FAR
 * 25d7:0004 pixel service (RTL:ef83 is its CALL, not the function entry).
 * The 8086 dispatcher reads the resident mode at DS:15cc, draw operation
 * at DS:29de+16 and optional signed clip rectangle at +18..+20. Its
 * mode-12h and mode-13h entries are 25d7:00d6 and 25d7:0133 respectively.
 * No glyph geometry, spacing or glyph bits are reconstructed here: the
 * original RTL:edff C translation produces every pixel call. */
void gate_rtl_plot_pixel(uint16_t color, uint16_t y, uint16_t x)
{
    uint16_t mode;
    uint16_t operation;
    uint8_t old;
    uint8_t incoming;
    uint8_t output;

    if (gate_ds_word(0x29de + 0x18) == 1) {
        if ((int16_t)x < (int16_t)gate_ds_word(0x29de + 0x1a) ||
            (int16_t)y < (int16_t)gate_ds_word(0x29de + 0x1c) ||
            (int16_t)x > (int16_t)gate_ds_word(0x29de + 0x1e) ||
            (int16_t)y > (int16_t)gate_ds_word(0x29de + 0x20))
            return;
    }

    /* Only the two original intro VGA modes are modeled. In other modes
     * the 25d7 dispatcher reaches different callbacks, which must be
     * recovered separately before source-C execution can use them. */
    mode = gate_ds_word(0x15cc);
    if (mode != 2 && mode != 3)
        abort();
    if ((mode == 2 && (x >= 640 || y >= 480)) ||
        (mode == 3 && (x >= 320 || y >= 200)))
        return; /* outside the modeled video aperture */

    operation = gate_ds_word(0x29de + 0x16) & 0xffu;
    incoming = (uint8_t)color;
    if (mode == 2) {
        incoming &= 15u;
        old = gate_intro_vga16_pixel(x, y);
    } else {
        old = gate_far_byte(0xa000, (uint16_t)(320u * y + x));
    }
    switch (operation) {
    case 0: output = incoming; break; /* copy */
    case 1: output = (uint8_t)(old & incoming); break;
    case 3: output = (uint8_t)(old ^ incoming); break;
    default: output = (uint8_t)(old | incoming); break;
    }

    if (mode == 2)
        gate_intro_vga16_set_pixel(x, y, output);
    else
        gate_far_set_byte(0xa000, (uint16_t)(320u * y + x), output);
}
