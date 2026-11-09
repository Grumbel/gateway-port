/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Host behavioral stand-ins for the DOS-only BIOS/resident entry callbacks.
 * The actual DOS BOOTVGA links RESSTATE.OBJ; this fixture exercises the
 * original mode setter and probe control flow without changing host video. */
#include "GATEMEM.H"
#include "RTLLAY.H"
#include <dos.h>
#include <stdlib.h>
#include <stdio.h>

static uint8_t mock_video_mode = 3u;
static unsigned mode_requests;
static unsigned starts;

static void require(int good)
{
    if (!good) {
        fputs("live VGA host fixture: unexpected BIOS or resident call\n", stderr);
        abort();
    }
}

int int86(int interrupt_number, union REGS *in, union REGS *out)
{
    uint16_t ax;
    require(interrupt_number == 0x10 && in != 0 && out != 0);
    ax = in->x.ax;
    if (ax == 0x0f00u) {
        out->h.al = mock_video_mode;
    } else if ((ax & 0xff00u) == 0u) {
        if (getenv("TEST_VIDEO_FAIL_RESTORE") == 0 || (ax & 0xffu) != 3u)
            mock_video_mode = (uint8_t)ax;
    } else {
        require(0);
    }
    return 0;
}

uint16_t gate_rtl_mode_set_bios(uint16_t mode)
{
    require(mode == 3u && ++mode_requests == 1u);
    gate_ds_set_word(GATE_DS_RENDERER_MODE, mode);
    return 0u;
}

uint16_t gate_rtl_mode_start(uint16_t enabled)
{
    union REGS in, out;
    require((enabled == 1u && starts == 0u) ||
            (enabled == 0u && starts == 1u));
    ++starts;
    gate_ds_set_word(GATE_DS_RENDERER_ENABLED, enabled);
    in.x.ax = enabled ? 0x0013u : 0x0003u;
    int86(0x10, &in, &out);
    return 0u;
}

void gate_rtl_plot_pixel(uint16_t color, uint16_t y, uint16_t x)
{
    require(color == 0x42u && y == 4u && x == 9u &&
            mock_video_mode == 0x13u);
    gate_far_set_byte(0xa000u, (uint16_t)(y * 320u + x),
                      getenv("TEST_VIDEO_BAD_PIXEL") != 0 ? 0x43u : (uint8_t)color);
}

void gate_rtl_wait_vertical_retrace(void)
{
    require(0); /* mode 3 must skip the VGA attribute-palette branch */
}
