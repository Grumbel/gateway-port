/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifndef GATE_INTRO_HOST_VIDEO_MODE_H
#define GATE_INTRO_HOST_VIDEO_MODE_H

#include <stdint.h>

/* Narrow resident BIOS/graphics service boundary for GATE.RTL:f14d.
 * A host must set the detected adapter and monitor explicitly: this is
 * hardware configuration, not original game logic or a mode timetable. */
typedef struct GateIntroVideoModeHardware {
    uint8_t adapter_type;
    uint8_t monitor_subtype;
    uint16_t svga_variant;
    uint16_t requested_bios_mode; /* original renderer selector (0..7) */
    uint8_t bios_mode;            /* INT 10h/AH=0fh AL, not renderer selector */
    uint8_t bios_mode_valid;      /* host supplied or mode-start initialized */
    uint16_t clip_left, clip_top, clip_right, clip_bottom;
    uint16_t clipping_enabled;
    uint16_t start_value;
    uint16_t fail_set_mode;
    uint16_t fail_start;
    uint32_t adapter_queries;
    uint32_t mode_changes;
    uint32_t bios_mode_sets;
    uint32_t start_calls;
    uint32_t clip_calls;
} GateIntroVideoModeHardware;

void gate_intro_video_mode_attach(GateIntroVideoModeHardware *hardware);
void gate_intro_video_mode_detach(void);
/* Require known BIOS video state when DS:15d2 requests an INT 10h check.
 * A snapshot-based host must supply it before calling the resident service. */
uint8_t gate_intro_video_mode_current_bios(void);

#endif
