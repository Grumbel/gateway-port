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

/* The original resident 25d7:0004 dispatches modes 1/2 to a VGA planar
 * pixel operation and mode 3 to a linear 320x200 pixel operation. This DOS
 * boundary uses BIOS INT 10h/0c/0d for planar pixels and the real A000
 * aperture for mode 13h. The original clipping and raster operations are
 * reconstructed in src/intro_host_rtl_pixel.c; uncommon modes 0 and 4
 * remain unimplemented and must not silently draw as mode 13h. */
void gate_rtl_plot_pixel(uint16_t color, uint16_t y, uint16_t x)
{
    uint16_t mode, operation;
    uint8_t incoming, old, result;
    if (gate_ds_word(GATE_DS_DRAW_CLIPPING_ENABLED) == 1u) {
        if ((int16_t)x < (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_LEFT) ||
            (int16_t)y < (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_TOP) ||
            (int16_t)x > (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_RIGHT) ||
            (int16_t)y > (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_BOTTOM))
            return;
    }
    mode = gate_ds_word(GATE_DS_RENDERER_MODE);
    if (mode != 1u && mode != 2u && mode != 3u)
        abort();
    if (((mode == 1u || mode == 2u) &&
         (x >= 640u || y >= (mode == 1u ? 350u : 480u))) ||
        (mode == 3u && (x >= 320u || y >= 200u)))
        return;

    operation = (uint16_t)(gate_ds_word(GATE_DS_DRAW_OPERATION) & 0xffu);
    incoming = (uint8_t)(mode == 3u ? color : (color & 15u));
    if (mode == 3u) {
        uint16_t offset = (uint16_t)(320ul * y + x);
        old = gate_far_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT, offset);
        switch (operation) {
        case 0u: result = incoming; break;
        case 1u: result = (uint8_t)(old & incoming); break;
        case 3u: result = (uint8_t)(old ^ incoming); break;
        default: result = (uint8_t)(old | incoming); break;
        }
        gate_far_set_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT, offset, result);
    } else {
        union REGS in, out;
        in.x.ax = 0u;
        in.x.bx = 0u;
        in.x.cx = x;
        in.x.dx = y;
        in.h.ah = 0x0du;
        int86(0x10, &in, &out);
        old = (uint8_t)(out.h.al & 0x0fu);
        switch (operation) {
        case 0u: result = incoming; break;
        case 1u: result = (uint8_t)(old & incoming); break;
        case 3u: result = (uint8_t)(old ^ incoming); break;
        default: result = (uint8_t)(old | incoming); break;
        }
        /* BIOS pixel write's high bit can mean XOR in planar modes; all
         * our explicitly combined colors are <= 0x0f (copy only). */
        in.h.ah = 0x0cu;
        in.h.al = result;
        int86(0x10, &in, &out);
    }
}

/* Original 2621:003e first updates the remembered pen, then rasterizes
 * from its prior position. The intro's recovered calls use axis-aligned
 * single-pixel-wide, unmasked edges; diagonal/masked paths are not yet
 * reconstructed and abort instead of drawing an invented line. */
void gate_rtl_resident_line_to(uint16_t x, uint16_t y)
{
    int32_t old_x = (int16_t)gate_ds_word(GATE_DS_DRAW_X);
    int32_t old_y = (int16_t)gate_ds_word(GATE_DS_DRAW_Y);
    int32_t next_x = (int16_t)x;
    int32_t next_y = (int16_t)y;
    int32_t cursor, step;
    uint16_t color = gate_ds_word(GATE_DS_DRAW_COLOR);
    gate_rtl_resident_move_to(x, y);
    if (gate_ds_word(GATE_DS_DRAW_LINE_WIDTH) != 1u ||
        gate_ds_word(GATE_DS_DRAW_LINE_MASK) != 0xffffu)
        abort();
    if (old_x == next_x) {
        step = old_y <= next_y ? 1 : -1;
        for (cursor = old_y;; cursor += step) {
            gate_rtl_plot_pixel(color, (uint16_t)cursor, x);
            if (cursor == next_y)
                break;
        }
    } else if (old_y == next_y) {
        step = old_x <= next_x ? 1 : -1;
        for (cursor = old_x;; cursor += step) {
            gate_rtl_plot_pixel(color, y, (uint16_t)cursor);
            if (cursor == next_x)
                break;
        }
    } else {
        abort();
    }
}

/* Resident 2698:0006's bounded style-1 outline and opaque style-2 solid
 * fill. These are the branches recovered in the native host and used by
 * the launcher. Patterned, transparent and reversed-endpoint paths remain
 * explicit missing behavior. */
void gate_rtl_background_rect(uint16_t style, uint16_t bottom,
                              uint16_t right, uint16_t top, uint16_t left)
{
    uint16_t saved_color, saved_mask, saved_width;
    uint16_t y;
    if ((style != 1u && style != 2u) ||
        gate_ds_word(GATE_DS_DRAW_FILL_PATTERN) != 0u ||
        gate_ds_word(GATE_DS_DRAW_FILL_TRANSPARENT) != 0u)
        abort();
    if (style == 1u) {
        uint16_t previous_x = gate_ds_word(GATE_DS_DRAW_X);
        uint16_t previous_y = gate_ds_word(GATE_DS_DRAW_Y);
        if (gate_ds_word(GATE_DS_DRAW_LINE_WIDTH) != 1u)
            abort();
        gate_ds_set_word(GATE_DS_DRAW_RECT_STATE, 0u);
        gate_rtl_resident_move_to(left, bottom);
        gate_rtl_resident_line_to(left, top);
        gate_rtl_resident_line_to(right, top);
        gate_rtl_resident_line_to(right, bottom);
        gate_rtl_resident_line_to(left, bottom);
        gate_rtl_resident_move_to(previous_x, previous_y);
        gate_ds_set_word(GATE_DS_DRAW_RECT_STATE, 1u);
        return;
    }
    if ((int16_t)(uint16_t)(bottom - top) <= 0)
        return;
    if ((int16_t)right < (int16_t)left)
        abort();

    saved_color = gate_ds_word(GATE_DS_DRAW_COLOR);
    saved_mask = gate_ds_word(GATE_DS_DRAW_LINE_MASK);
    saved_width = gate_ds_word(GATE_DS_DRAW_LINE_WIDTH);
    gate_ds_set_word(GATE_DS_DRAW_COLOR, gate_ds_word(GATE_DS_DRAW_FILL_COLOR));
    gate_ds_set_word(GATE_DS_DRAW_LINE_MASK, 0xffffu);
    gate_ds_set_word(GATE_DS_DRAW_LINE_WIDTH, 1u);
    for (y = top;; ++y) {
        uint16_t x;
        for (x = left;; ++x) {
            gate_rtl_plot_pixel(gate_ds_word(GATE_DS_DRAW_COLOR), y, x);
            if (x == right)
                break;
        }
        if (y == bottom)
            break;
    }
    gate_ds_set_word(GATE_DS_DRAW_COLOR, saved_color);
    gate_ds_set_word(GATE_DS_DRAW_LINE_MASK, saved_mask);
    gate_ds_set_word(GATE_DS_DRAW_LINE_WIDTH, saved_width);
}

/* 24a7:000c resident VFX draw bridge. The original performs direct VGA
 * writes; this bounded DOS host implementation follows the already
 * tested src/intro_host_vfx.c for the introductory mode-12h and mode-13h
 * CA00 image layouts. Header offsets are from gate_rtl_image.h, and
 * must remain fixed for compatibility with the original C record loader. */
enum {
    DOS_VFX_SIGNATURE = 0x00,
    DOS_VFX_PLANES = 0x12,
    DOS_VFX_BPP = 0x13,
    DOS_VFX_STRIDE = 0x14,
    DOS_VFX_PIXELS_OFFSET = 0x18,
    DOS_VFX_PIXELS_SEGMENT = 0x1a,
    DOS_VFX_WIDTH = 0x2c,
    DOS_VFX_HEIGHT = 0x2e,
    DOS_VFX_ROW_BYTES = 0x30
};

static uint16_t vfx_word(GateFarPtr image, uint16_t offset)
{
    return gate_far_word(image.segment, (uint16_t)(image.offset + offset));
}

/* Resident 244c:0000 rolls the segment ahead by 1000h on a 64KiB
 * read-offset boundary. Plain 8086 FAR read helpers do not do that. */
static uint8_t vfx_extended_byte(GateFarPtr data, uint32_t position)
{
    uint32_t linear = (uint32_t)data.offset + position;
    return gate_far_byte((uint16_t)(data.segment + ((linear >> 16) << 12)),
                         (uint16_t)linear);
}

static uint8_t vfx_raster_byte(uint8_t incoming, uint8_t old,
                               uint16_t operation)
{
    switch (operation) {
    case 0u: return incoming;
    case 1u: return (uint8_t)(old & incoming);
    case 3u: return (uint8_t)(old ^ incoming);
    default: return (uint8_t)(old | incoming);
    }
}

void gate_linked_blit_vfx_picture(uint16_t zero, uint16_t y, uint16_t x,
                                   uint16_t operation, GateFarPtr image)
{
    uint16_t mode = gate_ds_word(GATE_DS_RENDERER_MODE);
    uint16_t width, original_width, height, stride, row, col;
    int16_t available;
    GateFarPtr source;
    (void)zero;
    if (mode != 2u && mode != 3u)
        abort();
    if (vfx_word(image, DOS_VFX_SIGNATURE) != 0xca00u)
        return;

    width = vfx_word(image, DOS_VFX_WIDTH);
    original_width = width;
    height = vfx_word(image, DOS_VFX_HEIGHT);
    stride = vfx_word(image, DOS_VFX_STRIDE);
    source.segment = vfx_word(image, DOS_VFX_PIXELS_SEGMENT);
    source.offset = vfx_word(image, DOS_VFX_PIXELS_OFFSET);
    if (stride == 0u || source.segment == 0u)
        abort();
    available = (int16_t)(uint16_t)((mode == 2u ? 640u : 320u) - x);
    if (available <= 0)
        return;
    if ((int16_t)width > available)
        width = (uint16_t)available;
    available = (int16_t)(uint16_t)((mode == 2u ? 480u : 200u) - y);
    if (available <= 0)
        return;
    if ((int16_t)height > available)
        height = (uint16_t)available;
    if (width == 0u || height == 0u)
        return;

    if (mode == 3u) {
        uint16_t copy_bytes = vfx_word(image, DOS_VFX_ROW_BYTES);
        uint16_t dest = (uint16_t)(320ul * y + x);
        if (gate_far_byte(image.segment,
                (uint16_t)(image.offset + DOS_VFX_PLANES)) != 1u ||
            gate_far_byte(image.segment,
                (uint16_t)(image.offset + DOS_VFX_BPP)) != 8u)
            abort();
        if (width < original_width)
            copy_bytes = width;
        for (row = 0u; row < height; ++row) {
            for (col = 0u; col < copy_bytes; ++col) {
                uint16_t address = (uint16_t)(dest + col);
                uint8_t pixel = gate_far_byte(source.segment,
                    (uint16_t)(source.offset + col));
                uint8_t old = gate_far_byte(
                    GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT, address);
                gate_far_set_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT,
                    address, vfx_raster_byte(pixel, old, operation));
            }
            source.offset = (uint16_t)(source.offset + stride);
            dest = (uint16_t)(dest + 320u);
        }
    } else {
        /* Mode 12h uses four bitplanes per row in source memory, with
         * MSB-first bits. Use BIOS read/write to respect VGA latches and
         * preserve the caller's sequencer / graphics-controller state. */
        union REGS in, out;
        if (gate_far_byte(image.segment,
                (uint16_t)(image.offset + DOS_VFX_PLANES)) != 4u ||
            gate_far_byte(image.segment,
                (uint16_t)(image.offset + DOS_VFX_BPP)) != 1u)
            abort();
        for (row = 0u; row < height; ++row) {
            for (col = 0u; col < width; ++col) {
                uint16_t plane;
                uint32_t base = (uint32_t)row * 4u * stride + col / 8u;
                uint8_t mask = (uint8_t)(0x80u >> (col & 7u));
                uint8_t pixel = 0u;
                uint8_t old;
                for (plane = 0u; plane < 4u; ++plane)
                    if ((vfx_extended_byte(source,
                         base + (uint32_t)plane * stride) & mask) != 0u)
                        pixel = (uint8_t)(pixel | (uint8_t)(1u << plane));
                in.x.ax = 0x0d00u;
                in.x.bx = 0u;
                in.x.cx = (uint16_t)(x + col);
                in.x.dx = (uint16_t)(y + row);
                int86(0x10, &in, &out);
                old = (uint8_t)(out.h.al & 15u);
                in.h.ah = 0x0cu;
                in.h.al = (uint8_t)(vfx_raster_byte(pixel, old, operation) & 15u);
                int86(0x10, &in, &out);
            }
        }
    }
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
