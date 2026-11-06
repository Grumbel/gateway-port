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

/* Graphics-font background is not drawn by RTL:edff itself: it sets the
 * fill state through resident 25a3:0002 and calls rectangle 2698:0006 with
 * style 2. The original style-2 / fill-pattern-0 branch draws horizontal
 * lines at every y from top through bottom, both inclusive. No patterned
 * fills or border variants are currently used by the recovered C printer.
 * Reject those unimplemented resident paths instead of approximating them. */
void gate_rtl_background_setup(uint16_t pattern, uint16_t color,
                               uint16_t transparent)
{
    if (pattern != 0 || transparent != 0)
        abort();
    gate_ds_set_word(0x29de + 0x22, pattern);
    gate_ds_set_word(0x29de + 0x24, color);
    gate_ds_set_word(0x29de + 0x26, transparent);
}

void gate_rtl_background_rect(uint16_t style, uint16_t bottom,
                              uint16_t right, uint16_t top, uint16_t left)
{
    uint16_t previous_color;
    uint16_t previous_mask;
    uint16_t previous_width;
    uint16_t y;

    if (style != 2 || gate_ds_word(0x29de + 0x22) != 0 ||
        gate_ds_word(0x29de + 0x26) != 0)
        abort();
    /* 2698:0018 uses signed JG after subtracting top from bottom. */
    if ((int16_t)(uint16_t)(bottom - top) <= 0)
        return;
    if ((int16_t)right < (int16_t)left)
        abort(); /* reversed endpoints still need the 2621 line service */

    previous_color = gate_ds_word(0x29de + 0x0c);
    previous_mask = gate_ds_word(0x29de + 0x28);
    previous_width = gate_ds_word(0x29de + 0x2a);
    gate_ds_set_word(0x29de + 0x0c, gate_ds_word(0x29de + 0x24));
    gate_ds_set_word(0x29de + 0x28, 0xffffu);
    gate_ds_set_word(0x29de + 0x2a, 1);

    for (y = top;; ++y) {
        uint16_t x;
        for (x = left;; ++x) {
            gate_rtl_plot_pixel(gate_ds_word(0x29de + 0x0c), y, x);
            if (x == right)
                break;
        }
        if (y == bottom)
            break;
    }
    gate_ds_set_word(0x29de + 0x0c, previous_color);
    gate_ds_set_word(0x29de + 0x28, previous_mask);
    gate_ds_set_word(0x29de + 0x2a, previous_width);
}
