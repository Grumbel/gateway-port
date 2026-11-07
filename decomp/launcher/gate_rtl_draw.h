/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#ifndef GATE_LAUNCHER_RTL_DRAW_H
#define GATE_LAUNCHER_RTL_DRAW_H

#include "gate_exe.h"

/* Original GATE.RTL file offsets (not linked segment:offset addresses).
 * fbf5 returns the previous selected ink; fc23 selects a fill pattern;
 * fc37 is the five-argument rectangle wrapper used by the intro. */
uint16_t gate_rtl_fbf5(uint16_t color);
void gate_rtl_fc23(uint16_t pattern);
void gate_rtl_fc37(uint16_t style, uint16_t left, uint16_t top,
                   uint16_t right, uint16_t bottom);
void gate_rtl_ff99(uint16_t left, uint16_t top, uint16_t right,
                   uint16_t bottom, uint16_t style);

/* Linked resident calls, outside the original RTL C image. */
void gate_rtl_resident_set_pen_color(uint16_t color);
void gate_rtl_resident_move_to(uint16_t x, uint16_t y); /* 2621:000e */
void gate_rtl_resident_line_to(uint16_t x, uint16_t y); /* 2621:003e */

#endif
