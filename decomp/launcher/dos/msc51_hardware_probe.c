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
void gate_rtl_mode_query_adapter(uint8_t *adapter);
uint16_t gate_rtl_mode_svga_variant(void);
GateFarPtr gate_linked_transform_picture_palette_rgb(GateFarPtr source);
GateFarPtr gate_linked_transform_picture_palette_indexed(GateFarPtr source);
void gate_linked_initialize_intro_services(void);

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
    uint8_t adapter[8];
    union REGS in, out;
    uint16_t svga;
    GateFarPtr original_palette, rgb_palette, indexed_palette;
    uint16_t i;
    unsigned int rtl_segment = 0;
    int valid = 1;

    if (_dos_allocmem(0x1000u, &segment) != 0) {
        puts("Hardware probe: DOS segment allocation failed");
        return 1;
    }
    gate_dos_bind_ds((uint16_t)segment);
    /* Check the two independent, original RTL:c3bd early-return branches.
     * Never enter the still-unrecovered music-loader/IRQ8 path in a probe. */
    gate_ds_set_word(GATE_DS_MUSIC_STARTUP_CONFIG, 0x0401u);
    gate_far_set_byte((uint16_t)segment, GATE_DS_MUSIC_STARTUP_FLAGS, 0x08u);
    gate_linked_initialize_intro_services();
    valid &= check(gate_ds_word(GATE_DS_MUSIC_STARTUP_CONFIG), 0x0401u,
                   "music disabled preserves device configuration");
    gate_ds_set_word(GATE_DS_MUSIC_STARTUP_CONFIG, 0u);
    gate_far_set_byte((uint16_t)segment, GATE_DS_MUSIC_STARTUP_FLAGS, 0x06u);
    gate_linked_initialize_intro_services();
    valid &= check(gate_far_byte((uint16_t)segment, GATE_DS_MUSIC_STARTUP_FLAGS),
                   0x06u, "music unconfigured preserves trigger flags");
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

    /* This system has a VGA BIOS under DOSBox. Test the actual AX=1a00
     * display-combination query, including the original active/alternate
     * pair layout, without forcing a specific DOSBox video-card model. */
    memset(adapter, 0x5au, sizeof adapter);
    gate_rtl_mode_query_adapter(adapter);
    valid &= check(adapter[4], 0x5au, "untouched adapter packet tail");
    in.x.ax = 0x1a00u;
    in.x.bx = in.x.cx = in.x.dx = 0u;
    int86(0x10, &in, &out);
    valid &= check(out.h.al, 0x1au, "VGA display-combination BIOS");
    if (out.h.bl == 8u) {
        valid &= check(adapter[0], 5u, "VGA color adapter type");
        valid &= check(adapter[1], 5u, "VGA color monitor subtype");
    }
    svga = gate_rtl_mode_svga_variant();
    valid &= check(svga <= 5u ? 1u : 0u, 1u, "SVGA variant range");
    printf("Hardware probe: adapter %u/%u, alternate %u/%u, SVGA %u\n",
           adapter[0], adapter[1], adapter[2], adapter[3], svga);

    /* Both CRT functions are byte-level conversions, not palette I/O.
     * The first expands 16 packed colors to 48 RGB bytes at linked
     * DS:0fd8; the inverse repacks them to linked DS:1008. */
    original_palette.segment = (uint16_t)(segment + 0x10u);
    original_palette.offset = 0x0400u;
    for (i = 0u; i < 16u; ++i)
        gate_far_set_byte(original_palette.segment,
            (uint16_t)(original_palette.offset + i),
            (uint8_t)((i * 5u) & 63u));
    rgb_palette = gate_linked_transform_picture_palette_rgb(original_palette);
    valid &= check(rgb_palette.segment, (uint16_t)segment,
                   "RGB palette output segment");
    valid &= check(rgb_palette.offset,
                   (uint16_t)(GATE_DS_PALETTE_WORK_BASE + 0x000au),
                   "RGB palette output offset");
    indexed_palette = gate_linked_transform_picture_palette_indexed(rgb_palette);
    valid &= check(indexed_palette.offset,
                   (uint16_t)(GATE_DS_PALETTE_WORK_BASE + 0x003au),
                   "packed palette output offset");
    for (i = 0u; i < 16u; ++i)
        valid &= check(gate_far_byte(indexed_palette.segment,
                         (uint16_t)(indexed_palette.offset + i)),
                       (uint16_t)((i * 5u) & 63u),
                       "RGB/palette conversion round trip");

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
