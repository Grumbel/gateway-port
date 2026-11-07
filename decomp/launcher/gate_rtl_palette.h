/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#ifndef GATE_LAUNCHER_RTL_PALETTE_DECOMP_H
#define GATE_LAUNCHER_RTL_PALETTE_DECOMP_H

#include "gate_exe.h"

/* Original GATE.RTL file offsets. Unlike the reconstructed C++ preview,
 * these routines retain the original VGA port I/O and mode branching. */
void gate_rtl_wait_vertical_retrace(void); /* wait for vertical retrace transition */
void gate_rtl_upload_vga_palette(GateFarPtr palette); /* palette upload, 0cdc:04d2 */

/* Original relocated RTL data globals. DS offsets refer to the palette
 * driver's data segment (not necessarily the graphics driver's DS). */
uint16_t gate_rtl_palette_mode(void);             /* DS:0000 */
uint16_t gate_rtl_palette_video_type(void);       /* DS:0008 */
uint16_t gate_rtl_palette_status_port(void);      /* DS:0006 */
int16_t gate_rtl_palette_color_count(uint16_t offset); /* DS:0004+2*mode */
uint16_t gate_rtl_palette_conversion(void);       /* [DS:0018]->WORD */
uint8_t gate_rtl_palette_table_byte(uint16_t offset); /* DS:035c, DS:0366 */

/* Port access is the ONLY hardware boundary of the recovered VGA routine.
 * The byte read from the input port is the original AL value. */
uint8_t gate_rtl_palette_in(uint16_t port);
void gate_rtl_palette_out(uint16_t port, uint8_t value);

#endif
