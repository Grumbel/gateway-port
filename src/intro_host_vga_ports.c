/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "gate_rtl_palette.h"
#include "gate_intro_host_vga_ports.h"

#include <stdlib.h>

static GateIntroVgaPorts *active_ports;

void gate_intro_vga_ports_attach(GateIntroVgaPorts *ports)
{
    active_ports = ports;
}

void gate_intro_vga_ports_detach(void)
{
    active_ports = 0;
}

uint8_t gate_rtl_palette_in(uint16_t port)
{
    if (active_ports == 0 ||
        (port != GATE_VGA_PORT_COLOR_STATUS && port != GATE_VGA_PORT_MONO_STATUS))
        abort();

    /* The standalone host has no raster clock yet. A two-phase retrace
     * signal satisfies the original f473 busy-wait (clear, then set),
     * while recording every poll. This is *not* real-time VGA timing. */
    ++active_ports->status_reads;
    active_ports->retrace_phase ^= 1u;
    active_ports->attribute_expect_data = 0;
    return active_ports->retrace_phase ? 0 : GATE_VGA_RETRACE_STATUS_BIT;
}

void gate_rtl_palette_out(uint16_t port, uint8_t value)
{
    if (active_ports == 0)
        abort();
    switch (port) {
    case GATE_VGA_PORT_DAC_WRITE_INDEX:
        active_ports->dac_index = value;
        active_ports->dac_component = 0;
        return;
    case GATE_VGA_PORT_DAC_DATA:
        active_ports->dac[active_ports->dac_index]
                         [active_ports->dac_component] = value & GATE_VGA_DAC_COMPONENT_MASK;
        ++active_ports->dac_writes;
        if (++active_ports->dac_component == 3) {
            active_ports->dac_component = 0;
            ++active_ports->dac_index;
            /* Mode 12h uploads just 16 DAC triples (48 bytes). Waiting
             * for the 256-entry wrap dropped every intermediate logo
             * fade frame. Mode 13h still completes at the DAC wrap. */
            if (active_ports->palette_ready &&
                ((gate_rtl_palette_mode() == 2 && active_ports->dac_index == 16) ||
                 (gate_rtl_palette_mode() != 2 && active_ports->dac_index == 0)))
                active_ports->palette_ready(active_ports->palette_userdata);
        }
        return;
    case GATE_VGA_PORT_ATTRIBUTE_CONTROLLER:
        if (!active_ports->attribute_expect_data) {
            active_ports->attribute_index = value & GATE_VGA_ATTRIBUTE_INDEX_MASK;
            active_ports->display_enabled = (value & GATE_VGA_ATTRIBUTE_DISPLAY_ENABLE) != 0;
            active_ports->attribute_expect_data = value != GATE_VGA_ATTRIBUTE_DISPLAY_ENABLE;
        } else {
            active_ports->attributes[active_ports->attribute_index] = value;
            active_ports->attribute_expect_data = 0;
        }
        return;
    default:
        abort(); /* port not used by the source-C palette uploader */
    }
}
