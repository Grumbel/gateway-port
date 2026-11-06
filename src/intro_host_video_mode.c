/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "gate_rtl_mode.h"
#include "gate_intro_host_video_mode.h"

#include <stdlib.h>
#include <string.h>

static GateIntroVideoModeHardware *active_hardware;

void gate_intro_video_mode_attach(GateIntroVideoModeHardware *hardware)
{
    active_hardware = hardware;
}

void gate_intro_video_mode_detach(void)
{
    active_hardware = 0;
}

/* Resident 242a:000a provides the BIOS-detected adapter type and monitor.
 * The remaining packet bytes are not read by the original f14d. */
void gate_rtl_mode_query_adapter(uint8_t adapter[8])
{
    if (!active_hardware)
        abort();
    memset(adapter, 0, 8);
    adapter[0] = active_hardware->adapter_type;
    adapter[1] = active_hardware->monitor_subtype;
    ++active_hardware->adapter_queries;
}

uint16_t gate_rtl_mode_svga_variant(void)
{
    if (!active_hardware)
        abort();
    return active_hardware->svga_variant;
}

uint16_t gate_rtl_mode_set_bios(uint16_t mode)
{
    if (!active_hardware)
        abort();
    active_hardware->requested_bios_mode = mode;
    ++active_hardware->mode_changes;
    if (active_hardware->fail_set_mode != 0)
        return active_hardware->fail_set_mode;
    /* Resident 2409:0008 returns -6 for modes above seven, otherwise
     * writes the live renderer selector DS:15cc and returns zero. */
    if (mode > 7)
        return 0xfffau;
    gate_ds_set_word(0x15cc, mode);
    return 0;
}

uint16_t gate_rtl_mode_start(uint16_t enabled)
{
    if (!active_hardware)
        abort();
    active_hardware->start_value = enabled;
    ++active_hardware->start_calls;
    if (active_hardware->fail_start != 0)
        return active_hardware->fail_start;
    /* Resident 2409:003e records the enabled state and resets its BIOS
     * status word before performing an actual INT 10h mode transition.
     * The future SDL host will consume the selector and present video. */
    if (enabled != 0 && enabled != 1)
        return 0xfff9u;
    gate_ds_set_word(0x15ce, enabled);
    gate_ds_set_word(0x15d0, 0);
    return 0;
}

/* Original resident 25b7:003a stores X0/Y0/X1/Y1 at DS:29de +1a..+20.
 * It receives far stack arguments as bottom,right,top,left. The 25b7:0008
 * entry with argument 1 enables signed pixel clipping at +18. */
void gate_rtl_mode_clip(uint16_t bottom, uint16_t right,
                        uint16_t top, uint16_t left)
{
    if (!active_hardware)
        abort();
    active_hardware->clip_left = left;
    active_hardware->clip_top = top;
    active_hardware->clip_right = right;
    active_hardware->clip_bottom = bottom;
    ++active_hardware->clip_calls;
    gate_ds_set_word(0x29de + 0x1a, left);
    gate_ds_set_word(0x29de + 0x1c, top);
    gate_ds_set_word(0x29de + 0x1e, right);
    gate_ds_set_word(0x29de + 0x20, bottom);
}

void gate_rtl_mode_pen(uint16_t enabled)
{
    if (!active_hardware)
        abort();
    active_hardware->clipping_enabled = enabled == 1;
    gate_ds_set_word(0x29de + 0x18, enabled == 1 ? 1 : 0);
}
