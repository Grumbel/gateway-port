/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* An actual DOS executable probes the relocated clip state. The read-only
 * VGA status sample checks that IN is reachable under DOSBox, without
 * assuming a stable retrace bit. Port OUT is linked but not invoked here:
 * writing VGA registers for test purposes would alter display hardware. */
#include "GATEMEM.H"
#include "RTLLAY.H"
#include <dos.h>
#include <stdio.h>
#include <string.h>

uint8_t gate_rtl_palette_in(uint16_t port);
void gate_rtl_mode_clip(uint16_t bottom, uint16_t right,
                        uint16_t top, uint16_t left);
void gate_rtl_mode_pen(uint16_t enabled);
void gate_dos_bind_rtl_video_ds(uint16_t segment);
uint16_t gate_rtl_video_inversion(void);
uint8_t gate_rtl_video_attribute(void);
int16_t gate_rtl_video_columns(void);
int16_t gate_rtl_video_rows(void);
int16_t gate_rtl_video_tab_base(void);
uint16_t gate_rtl_video_block_mode(void);
void gate_rtl_video_set_block_mode(uint16_t mode);
void gate_rtl_video_command(uint16_t function, uint8_t *input, uint8_t *output);

static int check(uint16_t actual, uint16_t expected, const char *label)
{
    if (actual != expected) {
        printf("Hardware probe: %s got %04x expected %04x\n",
               label, actual, expected);
        return 0;
    }
    return 1;
}

int main(void)
{
    unsigned int segment = 0;
    uint8_t status;
    uint8_t packet[14];
    union REGS in, out;
    unsigned int rtl_segment = 0;
    int valid = 1;

    if (_dos_allocmem(0x1000u, &segment) != 0) {
        puts("Hardware probe: DOS segment allocation failed");
        return 1;
    }
    gate_dos_bind_ds((uint16_t)segment);
    if (_dos_allocmem(0x0040u, &rtl_segment) != 0) {
        puts("Hardware probe: RTL segment allocation failed");
        (void)_dos_freemem(segment);
        return 1;
    }
    gate_dos_bind_rtl_video_ds((uint16_t)rtl_segment);

    /* The three segment-pointer targets are distinct and live within the
     * EXE guest buffer. These are the original RTL DS indirections, not
     * the columns/rows words in the video DS itself. */
    gate_far_set_word((uint16_t)rtl_segment, 0u, 0x1234u);
    gate_far_set_byte((uint16_t)rtl_segment, 2u, 0x5au);
    gate_far_set_word((uint16_t)rtl_segment, 4u, (uint16_t)(segment + 0x10u));
    gate_far_set_word((uint16_t)rtl_segment, 6u, (uint16_t)(segment + 0x20u));
    gate_far_set_word((uint16_t)rtl_segment, 0x1au, (uint16_t)(segment + 0x30u));
    gate_far_set_word((uint16_t)(segment + 0x10u), 0u, 80u);
    gate_far_set_word((uint16_t)(segment + 0x20u), 0u, 25u);
    gate_far_set_word((uint16_t)(segment + 0x30u), 0u, 8u);
    gate_far_set_word((uint16_t)rtl_segment, 0x030eu, 1u);
    valid &= check(gate_rtl_video_inversion(), 0x1234u, "RTL inversion");
    valid &= check(gate_rtl_video_attribute(), 0x5au, "RTL attribute");
    valid &= check((uint16_t)gate_rtl_video_columns(), 80u, "RTL columns");
    valid &= check((uint16_t)gate_rtl_video_rows(), 25u, "RTL rows");
    valid &= check((uint16_t)gate_rtl_video_tab_base(), 8u, "RTL tab base");
    valid &= check(gate_rtl_video_block_mode(), 1u, "RTL block cursor");
    gate_rtl_video_set_block_mode(0x0902u);
    valid &= check(gate_far_word((uint16_t)rtl_segment, 0x030eu), 0x0902u,
                   "RTL block cursor write");

    /* BIOS cursor query and setting to the SAME location are safe under
     * DOSBox; writing the current cell back validates command 9 without
     * altering the visible console. */
    memset(packet, 0, sizeof packet);
    in.x.ax = 0x0300u;
    in.x.bx = 0u;
    int86(0x10, &in, &out);
    packet[1] = 3u;
    gate_rtl_video_command(0x10u, packet, packet);
    valid &= check(packet[6], out.h.dl, "text cursor column");
    valid &= check(packet[7], out.h.dh, "text cursor row");
    packet[1] = 2u;
    gate_rtl_video_command(0x10u, packet, packet);
    in.x.ax = 0x0800u;
    in.x.bx = 0u;
    int86(0x10, &in, &out);
    packet[0] = out.h.al;
    packet[2] = out.h.ah;
    packet[4] = 1u;
    packet[5] = 0u;
    packet[1] = 9u;
    gate_rtl_video_command(0x10u, packet, packet);

    gate_rtl_mode_clip(199u, 319u, 12u, 7u);
    valid &= check(gate_ds_word(GATE_DS_DRAW_CLIP_LEFT), 7u, "clip left");
    valid &= check(gate_ds_word(GATE_DS_DRAW_CLIP_TOP), 12u, "clip top");
    valid &= check(gate_ds_word(GATE_DS_DRAW_CLIP_RIGHT), 319u, "clip right");
    valid &= check(gate_ds_word(GATE_DS_DRAW_CLIP_BOTTOM), 199u, "clip bottom");
    gate_rtl_mode_pen(1u);
    valid &= check(gate_ds_word(GATE_DS_DRAW_CLIPPING_ENABLED), 1u, "clip enabled");
    gate_rtl_mode_pen(2u);
    valid &= check(gate_ds_word(GATE_DS_DRAW_CLIPPING_ENABLED), 0u,
                   "non-one clip mode disabled");

    status = gate_rtl_palette_in(0x03dau); /* VGA color status, dynamic */
    printf("Hardware probe: VGA status port 03da read %02x\n", status);
    if (_dos_freemem(rtl_segment) != 0 || _dos_freemem(segment) != 0) {
        puts("Hardware probe: DOS segment free failed");
        return 1;
    }
    puts(valid ? "Hardware probe: PASS" : "Hardware probe: FAIL");
    return valid ? 0 : 1;
}
