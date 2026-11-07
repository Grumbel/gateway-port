/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#ifndef GATE_LAUNCHER_RTL_DECOMP_H
#define GATE_LAUNCHER_RTL_DECOMP_H

#include "gate_exe.h"
#include "gate_rtl_video.h"

/* The recovered functions are named for ORIGINAL GATE.RTL file offsets.
 * They use the same segmented-memory intrinsics as gate_exe.c. */
uint16_t gate_rtl_8298(GateFarPtr text);     /* 12a5:2bba FAR strlen */
uint32_t gate_rtl_8fa2(void);                /* 12a5:38c4 elapsed clock */
uint32_t gate_rtl_933a(uint32_t lhs, uint32_t rhs); /* 12a5:3c5c multiply */
void gate_rtl_e771(void);                   /* free both font slots */
void gate_rtl_e79b(GateFarPtr font);         /* free one font slot */
GateFarPtr gate_rtl_e801(void);              /* evict/select font slot */
GateFarPtr gate_rtl_e873(uint16_t request);  /* load/select font */
/* Relocated original FAR allocator, used by the font loader. */
GateFarPtr gate_far_1e2c_0008(uint16_t bytes);
void gate_far_1e2c_0138(GateFarPtr pointer);
uint16_t gate_rtl_ebf7(void);                 /* font line advance */
uint16_t gate_rtl_ec0a(uint16_t character);   /* font character advance */
uint16_t gate_rtl_eca4(GateFarPtr text);      /* string advance */
void gate_rtl_ecea(GateFarPtr ink, GateFarPtr background); /* color getters */
void gate_rtl_ed01(uint16_t ink, int16_t background);     /* color setter */
void gate_rtl_ed12(GateFarPtr x, GateFarPtr y);           /* cursor getters */
void gate_rtl_ed39(int16_t x, int16_t y);    /* text cursor */
void gate_rtl_ed7e(GateFarPtr text);          /* string draw */
void gate_rtl_edff(uint16_t character);      /* glyph scan and pixel writes */

/* Original RTL data symbols and linked calls not yet independently named.
 * In the captured GATE.EXE layout, active_font is DS:54ca (far pointer),
 * text x/y are DS:550a/550c. Linker fixups, not the zero-filled RTL image,
 * establish these offsets; they are exposed as named C globals here. */
GateFarPtr gate_rtl_active_font(void);
uint16_t gate_rtl_tab_advance(void);
void gate_rtl_set_cursor(int16_t x, int16_t y);
int16_t gate_rtl_cursor_x(void);
int16_t gate_rtl_cursor_y(void);
int16_t gate_rtl_text_mode_pitch(void);
int16_t gate_rtl_tab_pitch(void);
int16_t gate_rtl_tab_limit(void);
int16_t gate_rtl_newline_pitch(void);
int16_t gate_rtl_background_color(void);
uint16_t gate_rtl_ink_color(void);
void gate_rtl_set_colors(uint16_t ink, int16_t background);
void gate_rtl_background_setup(uint16_t arg0, uint16_t arg1, uint16_t arg2);
/* The as-yet unnamed far rectangle service's ORIGINAL C argument order:
 * style, bottom, right, top, left (as inferred from the five pushes). */
void gate_rtl_background_rect(uint16_t style, uint16_t bottom,
                               uint16_t right, uint16_t top, uint16_t left);
/* Far pixel service's ORIGINAL C argument order, from its three pushes. */
void gate_rtl_plot_pixel(uint16_t color, uint16_t y, uint16_t x);

#endif
