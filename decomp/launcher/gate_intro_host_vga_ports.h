/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifndef GATE_INTRO_HOST_VGA_PORTS_H
#define GATE_INTRO_HOST_VGA_PORTS_H

#include <stdint.h>

/* Host hardware state for the original RTL palette routine's VGA ports.
 * Stored separately from guest RAM: 3c8/3c9 are internal DAC registers,
 * 3c0 is the attribute controller and 3da is a retrace input port.
 * The SDL presenter can read dac[] while rendering palette indices. */
typedef struct GateIntroVgaPorts {
    uint8_t dac[256][3];
    uint8_t attributes[32];
    uint8_t dac_index;
    uint8_t dac_component;
    uint8_t attribute_index;
    uint8_t attribute_expect_data;
    uint8_t display_enabled;
    uint8_t retrace_phase;
    uint32_t status_reads;
    uint32_t dac_writes;
    /* Optional host-only tap after a full 256-color DAC upload. */
    void (*palette_ready)(void *userdata);
    void *palette_userdata;
} GateIntroVgaPorts;

void gate_intro_vga_ports_attach(GateIntroVgaPorts *ports);
void gate_intro_vga_ports_detach(void);

#endif
