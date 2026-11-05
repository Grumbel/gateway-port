/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#ifndef GATE_LAUNCHER_RTL_DECOMP_H
#define GATE_LAUNCHER_RTL_DECOMP_H

#include "gate_exe.h"

/* The recovered functions are named for ORIGINAL GATE.RTL file offsets.
 * They use the same segmented-memory intrinsics as gate_exe.c. */
uint16_t gate_rtl_ebf7(void);                 /* font line advance */
uint16_t gate_rtl_ec0a(uint16_t character);   /* font character advance */
uint16_t gate_rtl_eca4(GateFarPtr text);      /* string advance */
void gate_rtl_ed39(int16_t x, int16_t y);    /* text cursor */
void gate_rtl_ed7e(GateFarPtr text);          /* string draw */

/* Original RTL data symbols and linked calls not yet independently named.
 * In the captured GATE.EXE layout, active_font is DS:54ca (far pointer),
 * text x/y are DS:550a/550c. Linker fixups, not the zero-filled RTL image,
 * establish these offsets; they are exposed as named C globals here. */
GateFarPtr gate_rtl_active_font(void);
uint16_t gate_rtl_tab_advance(void);
GateFarPtr gate_rtl_normalize_pointer(GateFarPtr ptr);
void gate_rtl_set_cursor(int16_t x, int16_t y);
int16_t gate_rtl_cursor_x(void);
int16_t gate_rtl_cursor_y(void);
int16_t gate_rtl_text_mode_pitch(void);
void gate_rtl_text_mode_set_cursor(uint16_t x, uint16_t y);
void gate_rtl_text_mode_get_cursor(int16_t *x, int16_t *y);
void gate_rtl_text_mode_print(GateFarPtr text);
void gate_rtl_edff(uint16_t character); /* original glyph drawer, pending */

#endif
