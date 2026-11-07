/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#ifndef GATE_LAUNCHER_RTL_MODE_H
#define GATE_LAUNCHER_RTL_MODE_H

#include "gate_exe.h"

/* Original numeric video-mode/adapter selectors from the RTL mode setter.
 * These are launcher mode indices (NOT INT 10h BIOS mode numbers). The
 * extended BIOS mode selected from variant 1..5 remains a separate value.
 * The original stores ffff before validating, even on rejected requests. */
enum {
    GATE_RTL_VIDEO_MODE_TEXT = 0,
    GATE_RTL_VIDEO_MODE_VGA_256 = 3,
    GATE_RTL_VIDEO_MODE_SVGA = 4,
    GATE_RTL_VIDEO_MAX_MODE = GATE_RTL_VIDEO_MODE_SVGA,
    GATE_RTL_VIDEO_INVALID_MODE = 0xffff,
    GATE_RTL_VIDEO_OTHER_SVGA_ADAPTER_TYPE = 4,
    GATE_RTL_VIDEO_SVGA_MONITOR_MINIMUM = 4,
    GATE_RTL_VIDEO_MONO_STATUS_MONITOR_TYPE = 4,
    GATE_RTL_VIDEO_4BIT_PEN_MASK = 0x0f,
    GATE_RTL_VIDEO_8BIT_PEN_MASK = 0xff,
};

/* The original mode-switch entry in GATE.RTL, linked as 0cdc:0004.
 * Its AX result is 1 for success and 0 for a rejected/failed mode. */
uint16_t gate_rtl_set_video_mode(int16_t requested_mode);

/* Near helper called at RTL:f25a to program the VGA attribute palette. */
void gate_rtl_configure_vga_palette(void);

/* Original linked resident hardware calls, with their identities preserved.
 * The query writes at least two bytes (adapter type, monitor subtype).
 * The original clip stack order, closest argument first, is bottom,
 * right, top, left. Hardware implementations live outside decomp/. */
void gate_rtl_mode_query_adapter(uint8_t adapter[8]); /* 242a:000a */
uint16_t gate_rtl_mode_svga_variant(void);             /* 241d:0004 */
uint16_t gate_rtl_mode_set_bios(uint16_t mode);        /* 2409:0008 */
uint16_t gate_rtl_mode_start(uint16_t enabled);       /* 2409:003e */
void gate_rtl_mode_clip(uint16_t bottom, uint16_t right,
                        uint16_t top, uint16_t left); /* 25b7:003a */
void gate_rtl_mode_pen(uint16_t mode);                /* 25b7:0008 */

#endif
