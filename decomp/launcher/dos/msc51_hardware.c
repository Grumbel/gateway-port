/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Real-mode Microsoft C 5.1 hardware boundary. This is DOS host code,
 * not a claim that the original GATE.RTL hardware routine was C source.
 * The corresponding native port maintains these same linked-DS words in
 * src/intro_host_video_mode.c. No dummy video-dispatch implementation. */
#include "GATEMEM.H"
#include "RTLLAY.H"
#include <conio.h>
#include <dos.h>
#include <stdlib.h>

/* The text-mode RTL bank's DS is NOT the EXE's relocated DS. The original
 * GATE.RTL:f8eb..fbf3 directly addresses offsets 0000, 0002, 0004, 0006,
 * 001a and 030e through its own DS. The eventual DOS launcher entry must
 * bind that segment after loading/relocating the RTL. Never fall back to
 * gate_ds_segment(): that would read unrelated EXE/renderer state. */
static uint16_t rtl_video_ds;

void gate_dos_bind_rtl_video_ds(uint16_t segment)
{
    rtl_video_ds = segment;
}

static uint16_t rtl_video_segment(void)
{
    if (rtl_video_ds == 0u)
        abort();
    return rtl_video_ds;
}

uint16_t gate_rtl_video_inversion(void)
{
    return gate_far_word(rtl_video_segment(), 0x0000u);
}

uint8_t gate_rtl_video_attribute(void)
{
    return gate_far_byte(rtl_video_segment(), 0x0002u);
}

int16_t gate_rtl_video_columns(void)
{
    return (int16_t)gate_far_word(gate_far_word(rtl_video_segment(), 0x0004u), 0u);
}

int16_t gate_rtl_video_rows(void)
{
    return (int16_t)gate_far_word(gate_far_word(rtl_video_segment(), 0x0006u), 0u);
}

int16_t gate_rtl_video_tab_base(void)
{
    return (int16_t)gate_far_word(gate_far_word(rtl_video_segment(), 0x001au), 0u);
}

uint16_t gate_rtl_video_block_mode(void)
{
    return gate_far_word(rtl_video_segment(), 0x030eu);
}

void gate_rtl_video_set_block_mode(uint16_t mode)
{
    gate_far_set_word(rtl_video_segment(), 0x030eu, mode);
}

/* GATE.RTL:f8eb..fbf3 makes 14-byte shared in/out packet calls with
 * function 0x10 and command 2 (move cursor), 3 (query cursor) or 9 (write
 * character/attribute). This is a bounded DOS BIOS adapter for precisely
 * those commands, NOT a reconstruction of the full resident 12a5:2d8a
 * dispatcher. Unknown requests fail explicitly. */
void gate_rtl_video_command(uint16_t function, uint8_t *input, uint8_t *output)
{
    union REGS in, out;
    if (function != 0x10u || input == 0 || output == 0)
        abort();
    in.x.ax = 0u;
    in.x.bx = 0u; /* BIOS display page zero */
    in.x.cx = 0u;
    in.x.dx = 0u;
    switch (input[1]) {
    case 2u: /* 1-based source cursor converted to zero-based packet */
        in.h.ah = 0x02u;
        in.h.dl = input[6];
        in.h.dh = input[7];
        break;
    case 3u:
        in.h.ah = 0x03u;
        break;
    case 9u:
        in.h.ah = 0x09u;
        in.h.al = input[0];
        in.h.bl = input[2];
        in.x.cx = (uint16_t)((uint16_t)input[4] | ((uint16_t)input[5] << 8));
        break;
    default:
        abort();
    }
    int86(0x10, &in, &out);
    if (input[1] == 3u) {
        output[6] = out.h.dl;
        output[7] = out.h.dh;
    }
}

uint8_t gate_rtl_palette_in(uint16_t port)
{
    /* conio.h's inp/outp are the Microsoft C 5.x real-mode port I/O API. */
    return (uint8_t)inp(port);
}

void gate_rtl_palette_out(uint16_t port, uint8_t value)
{
    (void)outp(port, value);
}

/* Original resident 25b7:003a receives bottom,right,top,left, and writes
 * these four WORDs at linked DS:29de + 1a..20. The other resident entry
 * 25b7:0008 sets the clipping enable field at +18 to (enabled == 1). */
void gate_rtl_mode_clip(uint16_t bottom, uint16_t right,
                        uint16_t top, uint16_t left)
{
    gate_ds_set_word(GATE_DS_DRAW_CLIP_LEFT, left);
    gate_ds_set_word(GATE_DS_DRAW_CLIP_TOP, top);
    gate_ds_set_word(GATE_DS_DRAW_CLIP_RIGHT, right);
    gate_ds_set_word(GATE_DS_DRAW_CLIP_BOTTOM, bottom);
}

void gate_rtl_mode_pen(uint16_t enabled)
{
    gate_ds_set_word(GATE_DS_DRAW_CLIPPING_ENABLED, enabled == 1 ? 1 : 0);
}

/* Original resident GATE.RTL:17c95..17d79 (the primary display-combination
 * probe called by 242a:000a). AX=1a00 returns BL=active/BH=alternate
 * display codes. Each maps to a two-byte (adapter, monitor) pair. The
 * caller's four first bytes hold both pairs; the other four are untouched.
 * An old BIOS without AX=1a00 takes several chipset-specific probes in
 * the original RTL; we must not silently claim its unknown result is VGA. */
static void write_display_pair(uint8_t *pair, uint8_t display)
{
    pair[0] = 0u;
    pair[1] = 0u;
    switch (display) {
    case 0u: break;
    case 1u: pair[0] = 1u; pair[1] = 1u; break;
    case 2u: pair[0] = 2u; pair[1] = 2u; break;
    case 4u: pair[0] = 3u; pair[1] = 3u; break;
    case 5u: pair[0] = 3u; pair[1] = 1u; break;
    case 7u: pair[0] = 5u; pair[1] = 4u; break;
    case 8u: pair[0] = 5u; pair[1] = 5u; break;
    case 10u: pair[0] = 4u; pair[1] = 3u; break;
    case 11u: pair[0] = 4u; pair[1] = 4u; break;
    case 12u: pair[0] = 4u; pair[1] = 5u; break;
    default: break;
    }
}

void gate_rtl_mode_query_adapter(uint8_t *adapter)
{
    union REGS in, out;
    if (adapter == 0)
        abort();
    in.x.ax = 0x1a00u;
    in.x.bx = 0u;
    in.x.cx = 0u;
    in.x.dx = 0u;
    int86(0x10, &in, &out);
    if (out.h.al != 0x1au)
        abort(); /* old BIOS detection requires the unported RTL fallback */
    write_display_pair(adapter, out.h.bl);
    write_display_pair(adapter + 2, out.h.bh);

    /* Original 17d5f..17d78 rejects the monochrome code when either
     * video pair has type 1, by clearing precisely that WORD. */
    if (adapter[0] == 1u)
        adapter[0] = adapter[1] = 0u;
    else if (adapter[2] == 1u)
        adapter[2] = adapter[3] = 0u;
}

/* GATE.RTL:17b4f..17c24, resident 241d:0004. Detects a known SVGA
 * extension from the BIOS query, option-ROM signatures or bank/CRTC ports.
 * Unlike the original failed probes, every port write is restored so the
 * test does not leave the user's current display in a different bank. */
uint16_t gate_rtl_mode_svga_variant(void)
{
    union REGS in, out;
    uint8_t old_bank, old_crtc_index, old_crtc_data;
    uint8_t sample;
    uint16_t result = 0u;

    in.x.ax = 0x6f00u;
    in.x.bx = 0u;
    in.x.cx = 0u;
    in.x.dx = 0u;
    int86(0x10, &in, &out);
    if (out.x.bx == 0x5637u)
        return 3u;
    if (gate_far_byte(0xc000u, 0x007du) == 'V' &&
        gate_far_byte(0xc000u, 0x007eu) == 'G' &&
        gate_far_byte(0xc000u, 0x007fu) == 'A' &&
        gate_far_byte(0xc000u, 0x0080u) == '=')
        return 2u;

    old_bank = (uint8_t)inp(0x03cdu);
    (void)outp(0x03cdu, 0xaau);
    sample = (uint8_t)inp(0x03cdu);
    if (sample == 0xaau) {
        (void)outp(0x03cdu, 0x55u);
        if ((uint8_t)inp(0x03cdu) == 0x55u)
            result = 1u;
    }
    (void)outp(0x03cdu, old_bank);
    if (result != 0u) {
        old_crtc_index = (uint8_t)inp(0x03d4u);
        (void)outp(0x03d4u, 0x33u);
        old_crtc_data = (uint8_t)inp(0x03d5u);
        (void)outp(0x03d5u, 0x0au);
        (void)outp(0x03d4u, 0x33u);
        if (((uint8_t)inp(0x03d5u) & 0x0fu) == 0x0au) {
            (void)outp(0x03d5u, 0x05u);
            (void)outp(0x03d4u, 0x33u);
            if (((uint8_t)inp(0x03d5u) & 0x0fu) == 0x05u)
                result = 5u;
        }
        (void)outp(0x03d4u, 0x33u);
        (void)outp(0x03d5u, old_crtc_data);
        (void)outp(0x03d4u, old_crtc_index);
        return result;
    }

    if (gate_far_byte(0xc000u, 0x0040u) == '3' &&
        gate_far_byte(0xc000u, 0x0041u) == '1')
        return 4u;
    return 0u;
}
