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

/* Original RTL:f61b..f826 DAC conversion operands, distinct from port
 * addresses above. Packed palette samples encode two-bit red/green/blue
 * levels across nonadjacent source bits; DAC output uses 0,21,42,63.
 * Lookup conversion instead assigns weights 2,2,2,1,1,1 to bits 0..5. */
enum {
    GATE_PALETTE_MODE_PACKED = 1,
    GATE_PALETTE_MODE_SKIP_OVERSCAN_WRITE = 3,
    GATE_PALETTE_COLOR_COUNT_TABLE_BASE = 4,
    GATE_PALETTE_COLOR_COUNT_TABLE_MODE_SHIFT = 1,
    GATE_PALETTE_RGB_COMPONENTS = 3,
    GATE_PALETTE_INITIAL_OVERSCAN_NONZERO = 15,
    GATE_PALETTE_TWO_BIT_DAC_STEP = 21,
    GATE_PALETTE_PACKED_HIGH_LEVEL_BIT = 2,
    GATE_PALETTE_PACKED_LOW_LEVEL_BIT = 1,
    GATE_PALETTE_PACKED_RED_HIGH_SHIFT = 1,
    GATE_PALETTE_PACKED_RED_LOW_SHIFT = 5,
    GATE_PALETTE_PACKED_GREEN_LOW_SHIFT = 4,
    GATE_PALETTE_PACKED_BLUE_HIGH_LEFT_SHIFT = 1,
    GATE_PALETTE_PACKED_BLUE_LOW_SHIFT = 3,
    GATE_PALETTE_LOOKUP_BIT_0 = 0x01,
    GATE_PALETTE_LOOKUP_BIT_1 = 0x02,
    GATE_PALETTE_LOOKUP_BIT_2 = 0x04,
    GATE_PALETTE_LOOKUP_BIT_3 = 0x08,
    GATE_PALETTE_LOOKUP_BIT_4 = 0x10,
    GATE_PALETTE_LOOKUP_BIT_5 = 0x20,
    GATE_PALETTE_LOOKUP_FIRST_THREE_WEIGHT = 2,
    GATE_PALETTE_LOOKUP_LAST_THREE_WEIGHT = 1,
    GATE_PALETTE_SIGNED_SAMPLE_DIVISOR = 31,
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
