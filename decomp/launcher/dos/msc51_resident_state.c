/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Original linked resident 2409, 25a3 and 2621 services. Only the
 * independently observed DS writes and real DOS BIOS mode transitions are
 * implemented. Pixel/line/blitter dispatch remains a separate boundary. */
#include "GATEMEM.H"
#include "RTLLAY.H"
#include <dos.h>
#include <stdlib.h>

/* 25a3:011e stores the low WORD at DS:29de+0c. */
void gate_rtl_resident_set_pen_color(uint16_t color)
{
    gate_ds_set_word(GATE_DS_DRAW_COLOR, color);
}

/* 2621:000e stores the current pen position in two resident WORDs. */
void gate_rtl_resident_move_to(uint16_t x, uint16_t y)
{
    gate_ds_set_word(GATE_DS_DRAW_X, x);
    gate_ds_set_word(GATE_DS_DRAW_Y, y);
}

/* 25a3:0002 accepts pattern 0..11 and records color and strict binary
 * transparency. Unknown patterns are not approximated as pattern zero. */
void gate_rtl_background_setup(uint16_t pattern, uint16_t color,
                               uint16_t transparent)
{
    if ((int16_t)pattern < 0 || (int16_t)pattern > 11)
        abort();
    gate_ds_set_word(GATE_DS_DRAW_FILL_PATTERN, pattern);
    gate_ds_set_word(GATE_DS_DRAW_FILL_COLOR, color);
    gate_ds_set_word(GATE_DS_DRAW_FILL_TRANSPARENT, transparent == 1u ? 1u : 0u);
}

/* 2409:0008 rejects indices above seven and sets the active descriptor. */
uint16_t gate_rtl_mode_set_bios(uint16_t mode)
{
    if (mode > 7u)
        return 0xfffau;
    gate_ds_set_word(GATE_DS_RENDERER_MODE, mode);
    return 0u;
}

static uint8_t bios_video_mode(void)
{
    union REGS in, out;
    in.h.ah = 0x0fu; /* INT 10h current video mode */
    int86(0x10, &in, &out);
    return out.h.al;
}

static void bios_set_video_mode(uint8_t mode)
{
    union REGS in, out;
    in.h.ah = 0;
    in.h.al = mode;
    int86(0x10, &in, &out);
}

/* Original resident 2409:003e: accepts only 0/1, clears DS:15d0 and
 * uses the live mode descriptor's BIOS number at DS:27ee+3e*mode+16h. */
uint16_t gate_rtl_mode_start(uint16_t enabled)
{
    if (enabled > 1u)
        return 0xfff9u;
    gate_ds_set_word(GATE_DS_RENDERER_ENABLED, enabled);
    gate_ds_set_word(GATE_DS_RENDERER_STATUS, 0u);
    if (enabled == 0u) {
        bios_set_video_mode(3u);
    } else {
        uint16_t mode = gate_ds_word(GATE_DS_RENDERER_MODE);
        uint16_t entry;
        uint8_t expected;
        if (mode > 7u)
            abort();
        entry = (uint16_t)(GATE_RESIDENT_MODE_DESCRIPTOR_BASE +
                           mode * GATE_RESIDENT_MODE_DESCRIPTOR_BYTES);
        expected = gate_far_byte(gate_ds_segment(),
                                 (uint16_t)(entry + GATE_RESIDENT_MODE_BIOS_NUMBER));
        if (bios_video_mode() != expected)
            bios_set_video_mode(expected);
    }
    return 0u;
}
