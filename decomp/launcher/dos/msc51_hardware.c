/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Real-mode Microsoft C 5.1 hardware boundary. This is DOS host code,
 * not a claim that the original GATE.RTL hardware routine was C source.
 * The corresponding native port maintains these same linked-DS words in
 * src/intro_host_video_mode.c. No dummy video-dispatch implementation. */
#include "GATEMEM.H"
#include "RTLLAY.H"
#include <conio.h>

uint8_t gate_rtl_palette_in(uint16_t port)
{
    /* conio.h's inp/outp are the Microsoft C 5.x real-mode port I/O API. */
    return (uint8_t)inp(port);
}

void gate_rtl_palette_out(uint16_t port, uint8_t value)
{
    (void)outp(port, value);
}

/* Original resident 25b7:003a receives bottom,right,top,left, and writes
 * these four WORDs at linked DS:29de + 1a..20. The other resident entry
 * 25b7:0008 sets the clipping enable field at +18 to (enabled == 1). */
void gate_rtl_mode_clip(uint16_t bottom, uint16_t right,
                        uint16_t top, uint16_t left)
{
    gate_ds_set_word(GATE_DS_DRAW_CLIP_LEFT, left);
    gate_ds_set_word(GATE_DS_DRAW_CLIP_TOP, top);
    gate_ds_set_word(GATE_DS_DRAW_CLIP_RIGHT, right);
    gate_ds_set_word(GATE_DS_DRAW_CLIP_BOTTOM, bottom);
}

void gate_rtl_mode_pen(uint16_t enabled)
{
    gate_ds_set_word(GATE_DS_DRAW_CLIPPING_ENABLED, enabled == 1 ? 1 : 0);
}
