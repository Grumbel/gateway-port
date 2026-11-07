/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#ifndef GATE_LAUNCHER_RTL_DRAW_H
#define GATE_LAUNCHER_RTL_DRAW_H

#include "gate_exe.h"

/* Original RTL:fc37 and ff99 style-bit tests. Rectangle style bit 7
 * switches to a fixed background setup; caption frame bits select its
 * mutually exclusive inset/outset edge-painting branches. The setup code
 * 10 is retained as a numeric backend selector because its wider resident
 * meaning has not yet been established. */
enum {
    GATE_RECTANGLE_USE_BACKGROUND_STYLE_BIT = 0x0080,
    GATE_RECTANGLE_BACKGROUND_SETUP_CODE = 10,
    GATE_RECTANGLE_BACKGROUND_SETUP_ENABLED = 1,
    GATE_CAPTION_FRAME_DRAW_OUTLINE_BIT = 0x0002,
    GATE_CAPTION_FRAME_DRAW_INSET_EDGE_BIT = 0x0004,
    GATE_CAPTION_FRAME_DRAW_OUTSET_EDGE_BIT = 0x0008,
    GATE_CAPTION_FRAME_SOLID_FILL_STYLE = 1,
    GATE_CAPTION_FRAME_INSET_FILL_STYLE = 2,
};

/* Original GATE.RTL file offsets (not linked segment:offset addresses).
 * fbf5 returns the previous selected ink; fc23 selects a fill pattern;
 * fc37 is the five-argument rectangle wrapper used by the intro. */
uint16_t gate_rtl_set_pen_color(uint16_t color);
void gate_rtl_set_fill_pattern(uint16_t pattern);
void gate_rtl_fill_rectangle(uint16_t style, uint16_t left, uint16_t top,
                   uint16_t right, uint16_t bottom);
void gate_rtl_draw_caption_frame(uint16_t left, uint16_t top, uint16_t right,
                   uint16_t bottom, uint16_t style);

/* Linked resident calls, outside the original RTL C image. */
void gate_rtl_resident_set_pen_color(uint16_t color);
void gate_rtl_resident_move_to(uint16_t x, uint16_t y); /* 2621:000e */
void gate_rtl_resident_line_to(uint16_t x, uint16_t y); /* 2621:003e */

#endif
