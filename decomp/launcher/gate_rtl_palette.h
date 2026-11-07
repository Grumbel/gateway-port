/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#ifndef GATE_LAUNCHER_RTL_PALETTE_DECOMP_H
#define GATE_LAUNCHER_RTL_PALETTE_DECOMP_H

#include "gate_exe.h"

/* The palette driver's two lookup tables are indexed relative to its
 * original relocated near-DS bank at 0fce, yielding linked DS:132a/1334.
 * Hardware ports below are x86 I/O addresses, NOT DS memory offsets. */
enum {
    GATE_PALETTE_TABLE_FIRST_COMPONENT_OFFSET = 0x035c,
    GATE_PALETTE_TABLE_SECOND_COMPONENT_OFFSET = 0x0366,
    GATE_VGA_PORT_ATTRIBUTE_CONTROLLER = 0x03c0,
    GATE_VGA_PORT_DAC_WRITE_INDEX = 0x03c8,
    GATE_VGA_PORT_DAC_DATA = 0x03c9,
    GATE_VGA_PORT_MONO_STATUS = 0x03ba,
    GATE_VGA_PORT_COLOR_STATUS = 0x03da,
    /* Original video-adapter class, not a VGA port or BIOS mode. */
    GATE_VGA_ADAPTER_VIDEO_TYPE = 5,
    GATE_VGA_RETRACE_STATUS_BIT = 0x08,
    GATE_VGA_ATTRIBUTE_OVERSCAN_REGISTER = 0x11,
    GATE_VGA_ATTRIBUTE_DISPLAY_ENABLE = 0x20,
    GATE_VGA_ATTRIBUTE_INDEX_MASK = 0x1f,
    GATE_VGA_DAC_COMPONENT_MASK = 0x3f,
    GATE_VGA_ATTRIBUTE_PALETTE_COLORS = 16,
};

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
