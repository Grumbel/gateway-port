/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "gate_rtl.h"
#include "gate_rtl_draw.h"
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

    /* The resident 25d7 dispatcher maps modes 1 and 2 to the SAME
     * 25d7:00d6 planar pixel callback. Mode 1 has a shorter 640x350
     * viewport; the source-mode switch supplies its clip rectangle.
     * Modes 0/4 still use separate, unmodeled callbacks. */
    mode = gate_ds_word(0x15cc);
    if (mode != 1 && mode != 2 && mode != 3)
        abort();
    if (((mode == 1 || mode == 2) &&
         (x >= 640 || y >= (mode == 1 ? 350 : 480))) ||
        (mode == 3 && (x >= 320 || y >= 200)))
        return; /* outside the modeled video aperture */

    operation = gate_ds_word(0x29de + 0x16) & 0xffu;
    incoming = (uint8_t)color;
    if (mode == 1 || mode == 2) {
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

    if (mode == 1 || mode == 2)
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
void gate_rtl_resident_set_pen_color(uint16_t color)
{
    /* The actual 25a3:011e resident body stores the low WORD at +0c. */
    gate_ds_set_word(0x29de + 0x0c, color);
}

/* Resident 2621:000e stores the current pen at DS:29de+12/+14.
 * The 2621:003e variant first reads the old position, writes the new one,
 * and sends both to 2621:0079. For the intro's axis-aligned frame edges,
 * use the existing native pixel host with signed clipping; the original
 * general diagonal/width/mask line rasterizer is not modeled here. */
void gate_rtl_resident_move_to(uint16_t x, uint16_t y)
{
    gate_ds_set_word(0x29de + 0x12, x);
    gate_ds_set_word(0x29de + 0x14, y);
}

void gate_rtl_resident_line_to(uint16_t x, uint16_t y)
{
    int32_t old_x = (int16_t)gate_ds_word(0x29de + 0x12);
    int32_t old_y = (int16_t)gate_ds_word(0x29de + 0x14);
    int32_t next_x = (int16_t)x;
    int32_t next_y = (int16_t)y;
    int32_t cursor;
    int32_t step;
    const uint16_t color = gate_ds_word(0x29de + 0x0c);
    gate_rtl_resident_move_to(x, y);
    if (gate_ds_word(0x29de + 0x2a) != 1 ||
        gate_ds_word(0x29de + 0x28) != 0xffffu)
        abort();
    if (old_x == next_x) {
        step = old_y <= next_y ? 1 : -1;
        for (cursor = old_y;; cursor += step) {
            gate_rtl_plot_pixel(color, (uint16_t)cursor, x);
            if (cursor == next_y)
                break;
        }
    } else if (old_y == next_y) {
        step = old_x <= next_x ? 1 : -1;
        for (cursor = old_x;; cursor += step) {
            gate_rtl_plot_pixel(color, y, (uint16_t)cursor);
            if (cursor == next_x)
                break;
        }
    } else {
        abort(); /* general Bresenham path remains a separate boundary */
    }
}

void gate_rtl_background_setup(uint16_t pattern, uint16_t color,
                               uint16_t transparent)
{
    /* Resident 25a3:0002 accepts 0..11 and normalizes its third WORD
     * to one only if it equals 1; the rectangle implementation below
     * still rejects unverified patterned drawing. */
    if ((int16_t)pattern < 0 || (int16_t)pattern > 11)
        abort();
    gate_ds_set_word(0x29de + 0x22, pattern);
    gate_ds_set_word(0x29de + 0x24, color);
    gate_ds_set_word(0x29de + 0x26, transparent == 1 ? 1u : 0u);
}

void gate_rtl_background_rect(uint16_t style, uint16_t bottom,
                              uint16_t right, uint16_t top, uint16_t left)
{
    uint16_t previous_color;
    uint16_t previous_mask;
    uint16_t previous_width;
    uint16_t y;

    if ((style != 1 && style != 2) || gate_ds_word(0x29de + 0x22) != 0 ||
        gate_ds_word(0x29de + 0x26) != 0)
        abort();
    if (style == 1) {
        /* Resident 2698:0006's style-1 path draws the four outline
         * edges with 2621:0079; unlike 2621:003e, that direct call does
         * not change the remembered cursor position at +12/+14. */
        uint16_t saved_x = gate_ds_word(0x29de + 0x12);
        uint16_t saved_y = gate_ds_word(0x29de + 0x14);
        if (gate_ds_word(0x29de + 0x2a) != 1)
            abort();
        gate_ds_set_word(0x29de + 0x10, 0);
        gate_rtl_resident_move_to(left, bottom);
        gate_rtl_resident_line_to(left, top);
        gate_rtl_resident_line_to(right, top);
        gate_rtl_resident_line_to(right, bottom);
        gate_rtl_resident_line_to(left, bottom);
        gate_rtl_resident_move_to(saved_x, saved_y);
        gate_ds_set_word(0x29de + 0x10, 1);
        return;
    }
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
