/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#ifndef GATE_LAUNCHER_RTL_VIDEO_DECOMP_H
#define GATE_LAUNCHER_RTL_VIDEO_DECOMP_H

#include "gate_exe.h"

/* Original 16-bit RTL file-offset identities. These are the text-mode
 * counterparts to the graphics font's RTL:0xedff rasterizer. */
void gate_rtl_draw_block_cursor(void);                       /* draw inverse block */
void gate_rtl_clear_video_display(void);                       /* clear display, redraw block cursor */
uint16_t gate_rtl_set_block_cursor_mode(uint16_t new_mode);      /* toggle block mode */
void gate_rtl_get_text_cell_cursor(uint16_t *x, uint16_t *y);   /* query 1-based cursor */
void gate_rtl_set_text_cell_cursor(uint16_t x, uint16_t y);    /* move 1-based cursor */
void gate_rtl_print_text_mode_string(GateFarPtr text);            /* text-mode string output */

/* DS globals and the linked video dispatcher, kept external until their
 * original definitions and linker relocation targets are recovered.
 * The dispatcher receives the SAME 14-byte buffer as input and output,
 * preceded by original constant 0x10 (five stack words in total). */
void gate_rtl_video_command(uint16_t function, uint8_t *input, uint8_t *output);
/* Original linked resident 2488:000a (page, fill color). The native host
 * implements its display-memory boundary, preserving its 16-bit status. */
uint16_t gate_linked_clear_resident_display(uint16_t page, uint16_t color);
uint16_t gate_rtl_video_block_mode(void);   /* RTL DS:0x030e */
void gate_rtl_video_set_block_mode(uint16_t mode);
uint16_t gate_rtl_video_inversion(void);    /* RTL DS:0x0000 */
uint8_t gate_rtl_video_attribute(void);     /* RTL DS:0x0002 */
int16_t gate_rtl_video_columns(void);      /* RTL DS:0x0004 -> far word */
int16_t gate_rtl_video_rows(void);         /* RTL DS:0x0006 -> far word */
int16_t gate_rtl_video_tab_base(void);     /* RTL DS:0x001a -> far word */

#endif
