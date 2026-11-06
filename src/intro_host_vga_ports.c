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
        (port != 0x3da && port != 0x3ba))
        abort();

    /* The standalone host has no raster clock yet. A two-phase retrace
     * signal satisfies the original f473 busy-wait (clear, then set),
     * while recording every poll. This is *not* real-time VGA timing. */
    ++active_ports->status_reads;
    active_ports->retrace_phase ^= 1u;
    active_ports->attribute_expect_data = 0;
    return active_ports->retrace_phase ? 0 : 8;
}

void gate_rtl_palette_out(uint16_t port, uint8_t value)
{
    if (active_ports == 0)
        abort();
    switch (port) {
    case 0x3c8:
        active_ports->dac_index = value;
        active_ports->dac_component = 0;
        return;
    case 0x3c9:
        active_ports->dac[active_ports->dac_index]
                         [active_ports->dac_component] = value & 0x3fu;
        ++active_ports->dac_writes;
        if (++active_ports->dac_component == 3) {
            active_ports->dac_component = 0;
            ++active_ports->dac_index;
        }
        return;
    case 0x3c0:
        if (!active_ports->attribute_expect_data) {
            active_ports->attribute_index = value & 0x1fu;
            active_ports->display_enabled = (value & 0x20u) != 0;
            active_ports->attribute_expect_data = value != 0x20u;
        } else {
            active_ports->attributes[active_ports->attribute_index] = value;
            active_ports->attribute_expect_data = 0;
        }
        return;
    default:
        abort(); /* port not used by the source-C palette uploader */
    }
}
