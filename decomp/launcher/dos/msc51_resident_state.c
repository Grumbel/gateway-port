/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Original linked resident 2409, 25a3 and 2621 services. Only the
 * independently observed DS writes and real DOS BIOS mode transitions are
 * implemented. Pixel/line/blitter dispatch remains a separate boundary. */
#include "GATEMEM.H"
#include "RTLLAY.H"
#include <conio.h>
#include <dos.h>
#include <stdlib.h>

/* 25a3:011e stores the low WORD at DS:29de+0c. */
void gate_rtl_resident_set_pen_color(uint16_t color)
{
    gate_ds_set_word(GATE_DS_DRAW_COLOR, color);
}

/* 2621:000e stores the current pen position in two resident WORDs. */
void gate_rtl_resident_move_to(uint16_t x, uint16_t y)
{
    gate_ds_set_word(GATE_DS_DRAW_X, x);
    gate_ds_set_word(GATE_DS_DRAW_Y, y);
}

/* 25a3:0002 accepts pattern 0..11 and records color and strict binary
 * transparency. Unknown patterns are not approximated as pattern zero. */
void gate_rtl_background_setup(uint16_t pattern, uint16_t color,
                               uint16_t transparent)
{
    if ((int16_t)pattern < 0 || (int16_t)pattern > 11)
        abort();
    gate_ds_set_word(GATE_DS_DRAW_FILL_PATTERN, pattern);
    gate_ds_set_word(GATE_DS_DRAW_FILL_COLOR, color);
    gate_ds_set_word(GATE_DS_DRAW_FILL_TRANSPARENT, transparent == 1u ? 1u : 0u);
}

/* 2409:0008 rejects indices above seven and sets the active descriptor. */
uint16_t gate_rtl_mode_set_bios(uint16_t mode)
{
    if (mode > 7u)
        return 0xfffau;
    gate_ds_set_word(GATE_DS_RENDERER_MODE, mode);
    return 0u;
}

static uint8_t bios_video_mode(void)
{
    union REGS in, out;
    in.h.ah = 0x0fu; /* INT 10h current video mode */
    int86(0x10, &in, &out);
    return out.h.al;
}

static void bios_set_video_mode(uint8_t mode)
{
    union REGS in, out;
    in.h.ah = 0;
    in.h.al = mode;
    int86(0x10, &in, &out);
}

/* Original resident 2409:003e: accepts only 0/1, clears DS:15d0 and
 * uses the live mode descriptor's BIOS number at DS:27ee+3e*mode+16h. */
uint16_t gate_rtl_mode_start(uint16_t enabled)
{
    if (enabled > 1u)
        return 0xfff9u;
    gate_ds_set_word(GATE_DS_RENDERER_ENABLED, enabled);
    gate_ds_set_word(GATE_DS_RENDERER_STATUS, 0u);
    if (enabled == 0u) {
        bios_set_video_mode(3u);
    } else {
        uint16_t mode = gate_ds_word(GATE_DS_RENDERER_MODE);
        uint16_t entry;
        uint8_t expected;
        if (mode > 7u)
            abort();
        entry = (uint16_t)(GATE_RESIDENT_MODE_DESCRIPTOR_BASE +
                           mode * GATE_RESIDENT_MODE_DESCRIPTOR_BYTES);
        expected = gate_far_byte(gate_ds_segment(),
                                 (uint16_t)(entry + GATE_RESIDENT_MODE_BIOS_NUMBER));
        if (bios_video_mode() != expected)
            bios_set_video_mode(expected);
    }
    return 0u;
}

/* A000 planar VGA does not expose each color plane through a linear
 * segment. Select each sequencer write mask explicitly and write full-byte
 * color bits in GC write mode 0. Save/restore the touched VGA registers so
 * the original resident's later pixel operations remain independently
 * configurable. */
static void fill_vga_planes(uint16_t start, uint16_t bytes, uint8_t color)
{
    uint16_t plane;
    uint32_t i;
    uint8_t old_seq_index = (uint8_t)inp(0x3c4u);
    uint8_t old_gc_index = (uint8_t)inp(0x3ceu);
    uint8_t old_map, old_enable, old_rotate, old_mode, old_bit_mask;
    (void)outp(0x3c4u, 2u);
    old_map = (uint8_t)inp(0x3c5u);
    (void)outp(0x3ceu, 1u);
    old_enable = (uint8_t)inp(0x3cfu);
    (void)outp(0x3ceu, 3u);
    old_rotate = (uint8_t)inp(0x3cfu);
    (void)outp(0x3ceu, 5u);
    old_mode = (uint8_t)inp(0x3cfu);
    (void)outp(0x3ceu, 8u);
    old_bit_mask = (uint8_t)inp(0x3cfu);

    (void)outp(0x3ceu, 1u); (void)outp(0x3cfu, 0u);
    (void)outp(0x3ceu, 3u); (void)outp(0x3cfu, 0u);
    (void)outp(0x3ceu, 5u); (void)outp(0x3cfu, 0u);
    (void)outp(0x3ceu, 8u); (void)outp(0x3cfu, 0xffu);
    for (plane = 0u; plane < 4u; ++plane) {
        uint8_t value = (color & (uint8_t)(1u << plane)) != 0u ? 0xffu : 0u;
        (void)outp(0x3c4u, 2u);
        (void)outp(0x3c5u, (uint8_t)(1u << plane));
        for (i = 0ul; i < (uint32_t)bytes; ++i)
            gate_far_set_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT,
                              (uint16_t)(start + (uint16_t)i), value);
    }
    (void)outp(0x3c4u, 2u); (void)outp(0x3c5u, old_map);
    (void)outp(0x3c4u, old_seq_index);
    (void)outp(0x3ceu, 1u); (void)outp(0x3cfu, old_enable);
    (void)outp(0x3ceu, 3u); (void)outp(0x3cfu, old_rotate);
    (void)outp(0x3ceu, 5u); (void)outp(0x3cfu, old_mode);
    (void)outp(0x3ceu, 8u); (void)outp(0x3cfu, old_bit_mask);
    (void)outp(0x3ceu, old_gc_index);
}

/* Linked resident 2488:000a, based on the original 2488:011a (text),
 * 0169 (planar) and 01d7 (linear) clear handlers reconstructed natively.
 * No headless VGA emulation: these writes go to actual DOS apertures. */
uint16_t gate_linked_clear_resident_display(uint16_t page, uint16_t color)
{
    uint16_t ds = gate_ds_segment();
    uint16_t mode, descriptor, aperture, page_offset;
    uint16_t height, pitch;
    uint32_t size, i;

    if (gate_far_byte(ds, GATE_DS_RESIDENT_MODE_INITIALIZED) != 1u) {
        uint16_t index;
        for (index = 0u; index < 8u; ++index) {
            uint16_t entry = (uint16_t)(GATE_RESIDENT_MODE_DESCRIPTOR_BASE +
                                 index * GATE_RESIDENT_MODE_DESCRIPTOR_BYTES);
            uint16_t target = index == 0u ? GATE_RESIDENT_CLEAR_TEXT_CALLBACK :
                              index == 3u ? GATE_RESIDENT_CLEAR_LINEAR_CALLBACK :
                                            GATE_RESIDENT_CLEAR_PLANAR_CALLBACK;
            gate_far_set_word(ds, (uint16_t)(entry + GATE_RESIDENT_MODE_CLEAR_CALLBACK_OFFSET), target);
            gate_far_set_word(ds, (uint16_t)(entry + GATE_RESIDENT_MODE_CLEAR_CALLBACK_SEGMENT),
                              GATE_RESIDENT_VIDEO_CALLBACK_SEGMENT_VALUE);
        }
        gate_far_set_byte(ds, GATE_DS_RESIDENT_MODE_INITIALIZED, 1u);
    }
    mode = gate_ds_word(GATE_DS_RENDERER_MODE);
    if (mode > 7u)
        return 0xfffau;
    descriptor = (uint16_t)(GATE_RESIDENT_MODE_DESCRIPTOR_BASE +
                             mode * GATE_RESIDENT_MODE_DESCRIPTOR_BYTES);
    if (gate_far_byte(ds, descriptor) != (uint8_t)mode)
        return 0xfc19u;
    if (gate_ds_word(GATE_DS_RENDERER_CHECK_BIOS_MODE) == 1u &&
        bios_video_mode() != gate_far_byte(ds,
            (uint16_t)(descriptor + GATE_RESIDENT_MODE_BIOS_NUMBER)))
        return 0xfff9u;

    height = gate_far_word(ds, (uint16_t)(descriptor + GATE_RESIDENT_MODE_HEIGHT));
    pitch = gate_far_word(ds, (uint16_t)(descriptor + GATE_RESIDENT_MODE_ROW_PITCH));
    aperture = gate_far_word(ds, (uint16_t)(descriptor + GATE_RESIDENT_MODE_APERTURE_SEGMENT));
    page_offset = (uint16_t)(page * (uint32_t)gate_far_word(ds,
        (uint16_t)(descriptor + GATE_RESIDENT_MODE_PAGE_STRIDE)));

    if (mode == 0u) {
        if (aperture != GATE_VIDEO_TEXT_APERTURE_SEGMENT ||
            (uint32_t)page_offset + GATE_VIDEO_TEXT_CLEAR_BYTES > 65536ul)
            abort();
        for (i = 0ul; i < GATE_VIDEO_TEXT_CLEAR_BYTES; ++i)
            gate_far_set_byte(aperture, (uint16_t)(page_offset + (uint16_t)i),
                              (color & 1u) ? 0xffu : 0u);
    } else if (mode == 3u) {
        size = (uint16_t)((uint16_t)(height * (uint32_t)gate_far_byte(ds,
            (uint16_t)(descriptor + GATE_RESIDENT_MODE_PLANE_COUNT))) * (uint32_t)pitch);
        size &= ~1ul;
        if (aperture != GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT ||
            (uint32_t)page_offset + size > 65536ul)
            abort();
        for (i = 0ul; i < size; ++i)
            gate_far_set_byte(aperture, (uint16_t)(page_offset + (uint16_t)i), (uint8_t)color);
    } else {
        size = (uint16_t)(height * (uint32_t)pitch);
        if (aperture != GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT ||
            (uint32_t)page_offset + size > 65536ul)
            abort();
        fill_vga_planes(page_offset, (uint16_t)size, (uint8_t)(color & 0x0fu));
    }
    return 0u;
}
