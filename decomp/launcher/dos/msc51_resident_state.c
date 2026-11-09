/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Original linked resident 2409, 25a3 and 2621 services. Only the
 * independently observed DS writes and real DOS BIOS mode transitions are
 * implemented. Pixel/line/blitter dispatch remains a separate boundary. */
#include "GATEMEM.H"
#include "RTLLAY.H"
#include "RTLIMAC.H"
#include <conio.h>
#include <dos.h>
#include <memory.h>
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

/* Original resident 25d7:0004 dispatches modes 1/2 to RTL:19a69's
 * four-plane VGA write-mode-2 pixel operation and mode 3 to RTL:19ac6's
 * linear 320x200 byte operation. The original clipping is reconstructed
 * in src/intro_host_rtl_pixel.c. Unsupported modes 0 and 4 abort. */
void gate_rtl_plot_pixel(uint16_t color, uint16_t y, uint16_t x)
{
    uint16_t mode, operation;
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
    if (mode == 3u) {
        uint8_t incoming = (uint8_t)color;
        uint8_t old, result;
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
        /* RTL:19a78..19ab3, original resident 25d7:00d6. The VGA read
         * loads its four latches; write mode 2 expands the low color
         * nibble into the selected planes while GC3 combines against
         * those latches. GC8 selects only the pixel's one bit. The
         * draw-state +0a page offset is added to the byte address. The
         * original relocated SI also reads the raster operation at +16. */
        uint16_t page_offset = gate_ds_word(GATE_DS_DRAW_PAGE_OFFSET);
        uint16_t address = (uint16_t)((uint32_t)y * 80u + x / 8u + page_offset);
        uint8_t mask = (uint8_t)(1u << (7u - (x & 7u)));
        outp(0x3ce, 8); outp(0x3cf, mask);
        outp(0x3ce, 5); outp(0x3cf, 2);
        outp(0x3ce, 3); outp(0x3cf, (uint8_t)(operation << 3));
        (void)gate_far_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT, address);
        gate_far_set_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT,
                          address, (uint8_t)color);
        outp(0x3ce, 8); outp(0x3cf, 0xff);
        outp(0x3ce, 5); outp(0x3cf, 0);
        outp(0x3ce, 3); outp(0x3cf, 0);
    }
}

/* Original RTL:19f2d's line router takes RTL:1a535's mode-13h branch
 * when the raster operation is COPY and the line is one pixel wide.
 * For equal Y, RTL:1a5d1..1a5e6 draws the inclusive horizontal span
 * with REP STOSB. Only dispatch here when the whole span survives the
 * current clipping rectangle; other cases retain the per-pixel boundary. */
static int mode13_copy_span(uint16_t left, uint16_t right, uint16_t y)
{
    if (gate_ds_word(GATE_DS_RENDERER_MODE) != 3u ||
        gate_ds_word(GATE_DS_DRAW_OPERATION) != 0u ||
        left > right || right >= 320u || y >= 200u)
        return 0;
    if (gate_ds_word(GATE_DS_DRAW_CLIPPING_ENABLED) == 1u &&
        ((int16_t)left < (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_LEFT) ||
         (int16_t)right > (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_RIGHT) ||
         (int16_t)y < (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_TOP) ||
         (int16_t)y > (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_BOTTOM)))
        return 0;
    return 1;
}

/* RTL:1a5a9..1a5cf writes a vertical COPY line with STOSB and advances
 * DI by 319 after each byte; RTL:1a569..1a617 uses two 16-bit Bresenham
 * loops for diagonals. Both include their endpoints. In particular the
 * original starts the error at 2*minor-major, changes the minor coordinate
 * when that error is nonnegative, and swaps the endpoints to draw left to
 * right. Only enter the recovered backend for fully visible lines; clipping
 * still belongs to the separate RTL:19f2d line-router boundary. */
static int mode13_copy_nonhorizontal(int32_t x0, int32_t y0,
                                     int32_t x1, int32_t y1,
                                     uint8_t color)
{
    int16_t dx, dy, error, major, minor, stride, i;
    int32_t tmp;
    uint16_t address;
    if (gate_ds_word(GATE_DS_RENDERER_MODE) != 3u ||
        gate_ds_word(GATE_DS_DRAW_OPERATION) != 0u ||
        x0 < 0 || x0 >= 320 || x1 < 0 || x1 >= 320 ||
        y0 < 0 || y0 >= 200 || y1 < 0 || y1 >= 200 ||
        y0 == y1)
        return 0;
    if (gate_ds_word(GATE_DS_DRAW_CLIPPING_ENABLED) == 1u &&
        (x0 < (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_LEFT) ||
         x1 < (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_LEFT) ||
         x0 > (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_RIGHT) ||
         x1 > (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_RIGHT) ||
         y0 < (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_TOP) ||
         y1 < (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_TOP) ||
         y0 > (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_BOTTOM) ||
         y1 > (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_BOTTOM)))
        return 0;

    if (x0 == x1) {
        /* Original RTL:1a5b1 first chooses the topmost Y. */
        if (y0 > y1) { tmp = y0; y0 = y1; y1 = tmp; }
        address = (uint16_t)(320l * y0 + x0);
        for (i = (int16_t)y0; i <= (int16_t)y1; ++i) {
            gate_far_set_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT,
                              address, color);
            address = (uint16_t)(address + 320u);
        }
        return 1;
    }

    if (x0 > x1) {
        tmp = x0; x0 = x1; x1 = tmp;
        tmp = y0; y0 = y1; y1 = tmp;
    }
    dx = (int16_t)(x1 - x0);
    dy = (int16_t)(y1 - y0);
    stride = dy < 0 ? -320 : 320;
    if (dy < 0) dy = -dy;
    address = (uint16_t)(320l * y0 + x0);
    if (dy <= dx) {
        /* RTL:1a5e8: the X-major loop's STOSB increments X each time. */
        major = dx;
        minor = dy;
        error = 2 * minor - major;
        for (i = 0; i <= major; ++i) {
            gate_far_set_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT,
                              address, color);
            ++address;
            if (error < 0)
                error += 2 * minor;
            else {
                error += 2 * minor - 2 * major;
                address = (uint16_t)(address + stride);
            }
        }
    } else {
        /* RTL:1a600: each STOSB advances X, undone when Y alone moves. */
        major = dy;
        minor = dx;
        error = 2 * minor - major;
        for (i = 0; i <= major; ++i) {
            gate_far_set_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT,
                              address, color);
            ++address; /* STOSB increments DI even in the Y-major case. */
            address = (uint16_t)(address + stride);
            if (error < 0) {
                error += 2 * minor;
                --address; /* RTL:1a60d undoes the X step. */
            } else {
                error += 2 * minor - 2 * major;
            }
        }
    }
    return 1;
}

/* RTL:1a39c sets GC0 to the pen, GC1 to 0f (set/reset in all four
 * planes), and GC3 to the COPY raster operation. RTL:1a517 restores
 * fixed defaults: GC0=0, GC1=0, GC3=0, GC8=ff. */
static void mode12_copy_line_begin(uint8_t color)
{
    outp(0x3ce, 0); outp(0x3cf, color);
    outp(0x3ce, 1); outp(0x3cf, 0x0f);
    outp(0x3ce, 3); outp(0x3cf, 0);
}

static void mode12_copy_line_end(void)
{
    outp(0x3ce, 0); outp(0x3cf, 0);
    outp(0x3ce, 1); outp(0x3cf, 0);
    outp(0x3ce, 3); outp(0x3cf, 0);
    outp(0x3ce, 8); outp(0x3cf, 0xff);
}

/* The original horizontal RTL:1a4ab/1a4b1/1a4b6 MOVSB copies from VGA
 * video memory back to itself with GC8 selecting which bits change. The
 * read loads all four VGA latches; GC0/GC1 replace the selected plane
 * bits with the pen color on the write. Do not omit the read. */
static void mode12_copy_masked_byte(uint16_t address)
{
    uint8_t old = gate_far_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT, address);
    gate_far_set_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT, address, old);
}

/* Original mode-12h vertical COPY branch RTL:1a425..1a44c (through the
 * VGA GC-backed line routine RTL:1a39c). GC8 selects one pixel within
 * each 80-byte scanline. The CPU OR read/modify/write is essential:
 * the read loads the VGA latches before the write. The original
 * coordinate helper RTL:1a37b uses the mode pitch, without the separate
 * drawing-state page offset used by the pixel callback. */
static int mode12_copy_vertical(int32_t x0, int32_t y0,
                                 int32_t x1, int32_t y1,
                                 uint8_t color)
{
    uint16_t address;
    uint8_t mask, old;
    int16_t y;
    int32_t tmp;
    if (gate_ds_word(GATE_DS_RENDERER_MODE) != 2u ||
        gate_ds_word(GATE_DS_DRAW_OPERATION) != 0u ||
        gate_ds_word(GATE_DS_DRAW_PAGE_OFFSET) != 0u ||
        x0 != x1 || x0 < 0 || x0 >= 640 ||
        y0 < 0 || y0 >= 480 || y1 < 0 || y1 >= 480)
        return 0;
    if (gate_ds_word(GATE_DS_DRAW_CLIPPING_ENABLED) == 1u &&
        (x0 < (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_LEFT) ||
         x0 > (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_RIGHT) ||
         y0 < (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_TOP) ||
         y1 < (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_TOP) ||
         y0 > (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_BOTTOM) ||
         y1 > (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_BOTTOM)))
        return 0;

    if (y0 > y1) { tmp = y0; y0 = y1; y1 = tmp; }
    address = (uint16_t)(80l * y0 + x0 / 8);
    mask = (uint8_t)(1u << (7u - (x0 & 7u)));
    mode12_copy_line_begin(color);
    outp(0x3ce, 8); outp(0x3cf, mask);
    for (y = (int16_t)y0; y <= (int16_t)y1; ++y) {
        old = gate_far_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT, address);
        gate_far_set_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT,
                          address, (uint8_t)(old | 0x08u));
        address = (uint16_t)(address + 80u);
    }
    mode12_copy_line_end();
    return 1;
}

/* RTL:1a44f..1a4b8 takes horizontal mode-12h COPY lines by computing
 * the left and right partial-byte masks, using VGA's GC8 mask for the
 * edge bytes, then copying unchanged full VGA bytes with REP MOVSB.
 * Source and destination both point at A000; it is the GC set/reset
 * state that supplies the new color. The 1-byte span intersects both
 * masks, matching the original CX=0 branch at RTL:1a4a4. */
static int mode12_copy_horizontal(int32_t x0, int32_t y0,
                                   int32_t x1, int32_t y1,
                                   uint8_t color)
{
    uint16_t first, last, address, i;
    uint8_t first_mask, last_mask;
    int32_t tmp;
    if (gate_ds_word(GATE_DS_RENDERER_MODE) != 2u ||
        gate_ds_word(GATE_DS_DRAW_OPERATION) != 0u ||
        gate_ds_word(GATE_DS_DRAW_PAGE_OFFSET) != 0u ||
        y0 != y1 || y0 < 0 || y0 >= 480 ||
        x0 < 0 || x0 >= 640 || x1 < 0 || x1 >= 640)
        return 0;
    if (gate_ds_word(GATE_DS_DRAW_CLIPPING_ENABLED) == 1u &&
        (x0 < (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_LEFT) ||
         x1 < (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_LEFT) ||
         x0 > (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_RIGHT) ||
         x1 > (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_RIGHT) ||
         y0 < (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_TOP) ||
         y0 > (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_BOTTOM)))
        return 0;

    if (x0 > x1) { tmp = x0; x0 = x1; x1 = tmp; }
    first = (uint16_t)(x0 / 8);
    last = (uint16_t)(x1 / 8);
    address = (uint16_t)(80l * y0 + first);
    first_mask = (uint8_t)(0xffu >> (x0 & 7u));
    last_mask = (uint8_t)(0xffu << (7u - (x1 & 7u)));
    mode12_copy_line_begin(color);
    if (first == last) {
        outp(0x3ce, 8); outp(0x3cf, first_mask & last_mask);
        mode12_copy_masked_byte(address);
    } else {
        if ((x0 & 7) != 0) {
            outp(0x3ce, 8); outp(0x3cf, first_mask);
            mode12_copy_masked_byte(address++);
            ++first;
        }
        if (first < last) {
            outp(0x3ce, 8); outp(0x3cf, 0xff);
            for (i = first; i < last; ++i)
                mode12_copy_masked_byte(address++);
        }
        outp(0x3ce, 8); outp(0x3cf, last_mask);
        mode12_copy_masked_byte(address);
    }
    mode12_copy_line_end();
    return 1;
}

/* RTL:1a3b6..1a422 sorts diagonal endpoints by X, selects the signed
 * 80-byte row stride, and initializes its 16-bit Bresenham error as
 * 2*minor-major. The shallow RTL:1a4ba..1a4f7 branch batches successive
 * bits in AH: it writes a VGA byte only when the bit mask wraps, Y changes,
 * or the last pixel has been accumulated. The steep RTL:1a4f9..1a515
 * branch writes each pixel then advances the row; ROR/ADC moves right by
 * one pixel on an error step. Both use the original GC8 mask and the
 * CPU OR ES:[DI],AL VGA-latch read/modify/write. */
static int mode12_copy_diagonal(int32_t x0, int32_t y0,
                                 int32_t x1, int32_t y1,
                                 uint8_t color)
{
    int16_t dx, dy, major, minor, error, twice_minor, correction;
    int16_t row_stride, i;
    uint16_t address;
    uint8_t mask, pending_mask, old;
    int wrapped, change_row;
    int32_t tmp;
    if (gate_ds_word(GATE_DS_RENDERER_MODE) != 2u ||
        gate_ds_word(GATE_DS_DRAW_OPERATION) != 0u ||
        gate_ds_word(GATE_DS_DRAW_PAGE_OFFSET) != 0u ||
        x0 == x1 || y0 == y1 ||
        x0 < 0 || x0 >= 640 || x1 < 0 || x1 >= 640 ||
        y0 < 0 || y0 >= 480 || y1 < 0 || y1 >= 480)
        return 0;
    if (gate_ds_word(GATE_DS_DRAW_CLIPPING_ENABLED) == 1u &&
        (x0 < (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_LEFT) ||
         x1 < (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_LEFT) ||
         x0 > (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_RIGHT) ||
         x1 > (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_RIGHT) ||
         y0 < (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_TOP) ||
         y1 < (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_TOP) ||
         y0 > (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_BOTTOM) ||
         y1 > (int16_t)gate_ds_word(GATE_DS_DRAW_CLIP_BOTTOM)))
        return 0;

    if (x0 > x1) {
        tmp = x0; x0 = x1; x1 = tmp;
        tmp = y0; y0 = y1; y1 = tmp;
    }
    dx = (int16_t)(x1 - x0);
    dy = (int16_t)(y1 - y0);
    row_stride = dy < 0 ? -80 : 80;
    if (dy < 0) dy = (int16_t)-dy;
    major = dx >= dy ? dx : dy;
    minor = dx >= dy ? dy : dx;
    twice_minor = (int16_t)(2 * minor);
    error = (int16_t)(twice_minor - major);
    correction = (int16_t)(twice_minor - 2 * major);
    address = (uint16_t)(80l * y0 + x0 / 8);
    mask = (uint8_t)(1u << (7u - (x0 & 7u)));
    mode12_copy_line_begin(color);

    if (dx >= dy) {
        pending_mask = 0u;
        for (i = 0; i <= major; ++i) {
            pending_mask = (uint8_t)(pending_mask | mask);
            wrapped = (mask & 1u) != 0u;
            mask = (uint8_t)((mask >> 1) | (wrapped ? 0x80u : 0u));
            change_row = error >= 0;
            if (wrapped || change_row || i == major) {
                outp(0x3ce, 8); outp(0x3cf, pending_mask);
                old = gate_far_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT,
                                    address);
                gate_far_set_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT,
                                  address, (uint8_t)(old | 8u));
                pending_mask = 0u;
            }
            if (wrapped) ++address;
            if (change_row) {
                error = (int16_t)(error + correction);
                address = (uint16_t)(address + row_stride);
            } else {
                error = (int16_t)(error + twice_minor);
            }
        }
    } else {
        for (i = 0; i <= major; ++i) {
            outp(0x3ce, 8); outp(0x3cf, mask);
            old = gate_far_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT,
                                address);
            gate_far_set_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT,
                              address, (uint8_t)(old | 8u));
            address = (uint16_t)(address + row_stride);
            if (error < 0) {
                error = (int16_t)(error + twice_minor);
            } else {
                error = (int16_t)(error + correction);
                wrapped = (mask & 1u) != 0u;
                mask = (uint8_t)((mask >> 1) | (wrapped ? 0x80u : 0u));
                if (wrapped) ++address;
            }
        }
    }
    mode12_copy_line_end();
    return 1;
}

/* Original 2621:003e first updates the remembered pen, then rasterizes
 * from its prior position. All mode-13h and mode-12h COPY line branches
 * are recovered for fully visible one-pixel, unmasked lines; clipping,
 * non-COPY diagonals, and wide or masked lines remain separate boundaries. */
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
    if (mode13_copy_nonhorizontal(old_x, old_y, next_x, next_y,
                                  (uint8_t)color))
        return;
    if (mode12_copy_vertical(old_x, old_y, next_x, next_y,
                             (uint8_t)color))
        return;
    if (mode12_copy_horizontal(old_x, old_y, next_x, next_y,
                               (uint8_t)color))
        return;
    if (mode12_copy_diagonal(old_x, old_y, next_x, next_y,
                             (uint8_t)color))
        return;
    if (old_x == next_x) {
        step = old_y <= next_y ? 1 : -1;
        for (cursor = old_y;; cursor += step) {
            gate_rtl_plot_pixel(color, (uint16_t)cursor, x);
            if (cursor == next_y)
                break;
        }
    } else if (old_y == next_y) {
        uint16_t left = (uint16_t)(old_x < next_x ? old_x : next_x);
        uint16_t right = (uint16_t)(old_x > next_x ? old_x : next_x);
        if (mode13_copy_span(left, right, y)) {
            gate_far_fill_bytes(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT,
                (uint16_t)((uint32_t)y * 320u + left),
                (uint8_t)color, (uint16_t)(right - left + 1u));
            return;
        }
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
        if (mode13_copy_span(left, right, y)) {
            /* RTL:1a742's solid style-2 rectangle sends one horizontal
             * line per row to RTL:19f2d, which takes REP STOSB here. */
            gate_far_fill_bytes(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT,
                (uint16_t)((uint32_t)y * 320u + left),
                (uint8_t)gate_ds_word(GATE_DS_DRAW_COLOR),
                (uint16_t)(right - left + 1u));
        } else if (mode12_copy_horizontal((int32_t)left, (int32_t)y,
                     (int32_t)right, (int32_t)y,
                     (uint8_t)gate_ds_word(GATE_DS_DRAW_COLOR))) {
            /* RTL:1a742 invokes a horizontal line for each fill row.
             * In planar mode RTL:1a44f handles the two partial bytes
             * and all enclosed full bytes through VGA GC8 and MOVSB. */
        } else {
            uint16_t x;
            for (x = left;; ++x) {
                gate_rtl_plot_pixel(gate_ds_word(GATE_DS_DRAW_COLOR), y, x);
                if (x == right)
                    break;
            }
        }
        if (y == bottom)
            break;
    }
    gate_ds_set_word(GATE_DS_DRAW_COLOR, saved_color);
    gate_ds_set_word(GATE_DS_DRAW_LINE_MASK, saved_mask);
    gate_ds_set_word(GATE_DS_DRAW_LINE_WIDTH, saved_width);
}

/* 24a7:000c resident VFX draw bridge. Original RTL:184e8 selects the video
 * backend; RTL:189a0..18a2c is its mode-13h byte-per-pixel callback. Its
 * copy branch (189ca..189de) moves whole words and then the odd byte with
 * REP MOVSW/MOVSB; XOR/AND/OR use byte operations. This implementation
 * retains that row-copy distinction, using MSC 5.1 movedata for its native
 * segment:offset transfer. The byte-aligned four-plane mode-12h path below
 * follows RTL:187da..18959 (both aligned and unaligned branches) and the
 * register cleanup at RTL:1895c..1898d. */
enum {
    DOS_VFX_SIGNATURE = 0x00,
    DOS_VFX_PLANES = 0x12,
    DOS_VFX_BPP = 0x13,
    DOS_VFX_STRIDE = 0x14,
    DOS_VFX_PIXELS_OFFSET = 0x18,
    DOS_VFX_PIXELS_SEGMENT = 0x1a,
    DOS_VFX_WIDTH = 0x2c,
    DOS_VFX_HEIGHT = 0x2e,
    DOS_VFX_ROW_BYTES = 0x30,
    DOS_VFX_LAST_BYTE_MASK = 0x32
};

static uint16_t vfx_word(GateFarPtr image, uint16_t offset)
{
    return gate_far_word(image.segment, (uint16_t)(image.offset + offset));
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

static void vfx_mode12_ports_start(uint16_t operation)
{
    outp(0x3ce, 5); outp(0x3cf, 8);
    outp(0x3ce, 7); outp(0x3cf, 0);
    outp(0x3ce, 8); outp(0x3cf, 0xff);
    outp(0x3c4, 2); outp(0x3c5, 0x0f);
    outp(0x3c4, 2); outp(0x3c5, 1);
    outp(0x3ce, 3); outp(0x3cf, (uint8_t)(operation << 3));
}

static void vfx_mode12_ports_finish(void)
{
    /* RTL:1895c resets known VGA defaults, not previous register values. */
    outp(0x3ce, 5); outp(0x3cf, 0);
    outp(0x3ce, 7); outp(0x3cf, 0x0f);
    outp(0x3ce, 8); outp(0x3cf, 0xff);
    outp(0x3c4, 2); outp(0x3c5, 0x0f);
    outp(0x3ce, 3); outp(0x3cf, 0);
}

static void vfx_mode12_mask(uint8_t mask)
{
    outp(0x3ce, 8);
    outp(0x3cf, mask);
}

static void vfx_mode12_and_byte(uint16_t address, uint8_t incoming)
{
    uint8_t latched = gate_far_byte(
        GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT, address);
    gate_far_set_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT, address,
                      (uint8_t)(latched & incoming));
}

static uint16_t vfx_rotate_left_word(uint16_t value, uint16_t count)
{
    if (count == 0u)
        return value;
    return (uint16_t)((value << count) | (value >> (16u - count)));
}

/* Original RTL:187e9..188b2, byte-aligned mode-12h VFX rasterizer.
 * GC5=08 selects read mode 1 and write mode 0; with GC7=0, VGA reads
 * return FF and load the four latches. Hence the original instruction
 * AND BYTE PTR ES:[DI],AL writes the source byte to the selected plane,
 * and GC3 supplies the requested raster operation against the latches.
 * The source image groups all four planes of a row before the next row.
 * Do not replace this read/AND/write with a direct copy: on planar VGA
 * the register state and latch side effects are part of the operation. */
static void vfx_mode12_aligned(uint16_t x, uint16_t y, uint16_t height,
                                uint16_t stride, uint16_t copy_bytes,
                                uint16_t operation, GateFarPtr source)
{
    uint16_t row, plane, col;
    uint16_t destination = (uint16_t)((uint32_t)y * 80u + x / 8u);

    vfx_mode12_ports_start(operation);

    for (row = 0u; row < height; ++row) {
        for (plane = 0u; plane < 4u; ++plane) {
            uint16_t plane_source = (uint16_t)(source.offset +
                (uint32_t)(row * 4u + plane) * stride);
            uint8_t plane_mask = (uint8_t)(1u << plane);
            if (row != 0u || plane != 0u) {
                outp(0x3c4, 2);
                outp(0x3c5, plane_mask);
            }
            for (col = 0u; col < copy_bytes; ++col) {
                uint16_t address = (uint16_t)(destination + col);
                uint8_t incoming = gate_far_byte(source.segment,
                    (uint16_t)(plane_source + col));
                vfx_mode12_and_byte(address, incoming);
            }
        }
        destination = (uint16_t)(destination + 80u);
    }

    vfx_mode12_ports_finish();
}

/* Original RTL:188b4..18959, unaligned/masked four-plane path. The
 * source is advanced one byte between overlapping LODSW operations, then
 * ROL AX,CL supplies adjacent screen bytes. Two GC8 masks from the image
 * header's last-byte mask protect the right edge, even when the X origin
 * is already byte-aligned but the picture width is not. For a one-byte
 * source row, the original takes a separate mask setup branch at 188c0.
 * Read/write and port order follow the original, including writes with
 * GC8=0 (a masked no-op on VGA). */
static void vfx_mode12_masked(uint16_t x, uint16_t y, uint16_t height,
                               uint16_t stride, uint16_t copy_bytes,
                               uint8_t last_mask, uint16_t operation,
                               GateFarPtr source)
{
    uint16_t row, plane;
    uint16_t shift = (uint16_t)((8u - (x & 7u)) & 7u);
    uint16_t destination = (uint16_t)((uint32_t)y * 80u + x / 8u);
    uint16_t end_word;
    uint16_t start_word;
    uint8_t start_mask, end_mask1, end_mask2;

    if (copy_bytes == 1u) {
        start_word = last_mask;
        end_word = (uint16_t)last_mask << 8;
    } else {
        start_word = 0xffu;
        /* RTL:188b7's MOV AL,CH; CBW preserves the high sign bits. */
        end_word = (uint16_t)((last_mask & 0x80u ? 0xff00u : 0u) | last_mask);
    }
    start_word = (uint16_t)(start_word << shift);
    end_word = (uint16_t)(end_word << shift);
    start_mask = (uint8_t)(start_word >> 8);
    end_mask1 = (uint8_t)(end_word >> 8);
    end_mask2 = (uint8_t)end_word;

    vfx_mode12_ports_start(operation);
    for (row = 0u; row < height; ++row) {
        for (plane = 0u; plane < 4u; ++plane) {
            uint16_t cursor = (uint16_t)(source.offset +
                (uint32_t)(row * 4u + plane) * stride);
            uint16_t screen = destination;
            uint16_t data;
            int16_t remaining = (int16_t)copy_bytes;
            if (row != 0u || plane != 0u) {
                outp(0x3c4, 2);
                outp(0x3c5, (uint8_t)(1u << plane));
            }
            vfx_mode12_mask(start_mask);
            data = gate_far_word(source.segment, cursor);
            ++cursor; /* LODSW; DEC SI: sliding 16-bit source window */
            if (shift != 0u) {
                data = vfx_rotate_left_word(data, shift);
                vfx_mode12_and_byte(screen++, (uint8_t)(data >> 8));
                --remaining;
            } else {
                --remaining;
                if (remaining == 0)
                    goto masked_tail;
            }

            vfx_mode12_mask(0xffu);
            --remaining;
            while (remaining > 0) {
                vfx_mode12_and_byte(screen++, (uint8_t)data);
                data = gate_far_word(source.segment, cursor);
                ++cursor;
                data = vfx_rotate_left_word(data, shift);
                --remaining;
            }
masked_tail:
            vfx_mode12_mask(end_mask1);
            vfx_mode12_and_byte(screen, (uint8_t)data);
            vfx_mode12_mask(end_mask2);
            vfx_mode12_and_byte((uint16_t)(screen + 1u),
                                (uint8_t)(data >> 8));
        }
        destination = (uint16_t)(destination + 80u);
    }
    vfx_mode12_ports_finish();
}

/* RTL:11078 (save) and RTL:10f64 (restore), original FAR entries
 * 23bd:04d9 and 23bd:03c5. The original chooses the mouse driver before
 * the video-buffer path when both sets of flags are present. The linked
 * INT dispatcher RTL:8468 invokes INT 33h with AX=2 to hide and AX=1 to
 * show the cursor; these calls must remain paired, since the driver uses
 * a visibility counter rather than an idempotent on/off flag.
 *
 * The video-buffer branch (flags & 0x0c) invokes RTL:18a95 with two
 * overlapping region descriptors, and RTL:10dc7 to shift the saved area.
 * It is not equivalent to the image blitter below. Stop on that branch
 * until the descriptor contents and transfer directions are verified.
 * In particular, never manufacture a successful save for that mode. */
static void vfx_mouse_visibility(uint16_t function)
{
    union REGS in, out;
    in.x.ax = function;
    in.x.bx = 0u;
    in.x.cx = 0u;
    in.x.dx = 0u;
    int86(0x33, &in, &out);
}

uint16_t gate_linked_save_vfx_draw_state(void)
{
    uint16_t flags = gate_ds_word(GATE_DS_VFX_STATE_FLAGS);
    if (flags == 0u || (flags & 1u) == 0u)
        return 0u;
    if ((flags & 0x30u) == 0u && (flags & 0x0cu) != 0u)
        abort();
    gate_ds_set_word(GATE_DS_VFX_STATE_FLAGS, (uint16_t)(flags & ~1u));
    if ((flags & 0x30u) != 0u)
        vfx_mouse_visibility(2u);
    return 1u;
}

void gate_linked_restore_vfx_draw_state(uint16_t saved)
{
    uint16_t flags = gate_ds_word(GATE_DS_VFX_STATE_FLAGS);
    if (flags == 0u || (flags & 1u) != 0u || saved == 0u)
        return;
    if ((flags & 0x30u) == 0u && (flags & 0x0cu) != 0u)
        abort();
    gate_ds_set_word(GATE_DS_VFX_STATE_FLAGS, (uint16_t)(flags | 1u));
    if ((flags & 0x30u) != 0u)
        vfx_mouse_visibility(1u);
}

/* The two cleanup calls in original RTL:10da1..10dc6 target the
 * independently matched CA00 image destructor at RTL:180fe..18153.
 * In the native launcher the same operation is gate_rtl_graphics_release.
 * Its FAR pixel block is independently allocated; the header is NOT. */
uint16_t gate_linked_release_vfx_pixels(GateFarPtr pointer);

static void release_control_vfx_image(uint16_t record)
{
    uint16_t ds = gate_ds_segment();
    GateFarPtr pixels;
    uint16_t index;
    if (gate_far_word(ds, record) != GATE_VFX_IMAGE_SIGNATURE_VALUE)
        return;
    pixels.offset = gate_far_word(ds, (uint16_t)(record + GATE_VFX_IMAGE_PIXELS_OFFSET));
    pixels.segment = gate_far_word(ds, (uint16_t)(record + GATE_VFX_IMAGE_PIXELS_SEGMENT));
    if (gate_linked_release_vfx_pixels(pixels) != 0u)
        return;
    for (index = 0u; index < GATE_VFX_IMAGE_HEADER_BYTES; ++index)
        gate_far_set_byte(ds, (uint16_t)(record + index), 0xffu);
}

/* RTL:10da1, linked 1e48:0202 in the observed 22-second intro.
 * The first near call, RTL:11078, saves/hides the active drawing state.
 * Our save adapter fails closed on the unverified video-buffer save path;
 * the tested shutdown cases have the active-drawing bit cleared. Then the
 * original checks flags & 0c, releases the two 0x80-byte image records
 * in order and clears the entire VFX state word even if release failed. */
void gate_linked_vfx_shutdown(void)
{
    (void)gate_linked_save_vfx_draw_state();
    if ((gate_ds_word(GATE_DS_VFX_STATE_FLAGS) & 0x000cu) != 0u) {
        release_control_vfx_image(GATE_DS_CONTROL_VFX_IMAGE_FIRST);
        release_control_vfx_image(GATE_DS_CONTROL_VFX_IMAGE_SECOND);
    }
    gate_ds_set_word(GATE_DS_VFX_STATE_FLAGS, 0u);
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
            if (operation == 0u) {
                /* Original RTL:189ca..189d3 uses SHR CX,1; REP MOVSW;
                 * RCL CX,1; REP MOVSB, preserving the odd trailing byte.
                 * It intentionally does not read any destination pixels. */
                movedata(source.segment, source.offset,
                         GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT, dest,
                         copy_bytes);
            } else {
                for (col = 0u; col < copy_bytes; ++col) {
                    uint16_t address = (uint16_t)(dest + col);
                    uint8_t pixel = gate_far_byte(source.segment,
                        (uint16_t)(source.offset + col));
                    uint8_t old = gate_far_byte(
                        GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT, address);
                    gate_far_set_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT,
                        address, vfx_raster_byte(pixel, old, operation));
                }
            }
            source.offset = (uint16_t)(source.offset + stride);
            dest = (uint16_t)(dest + 320u);
        }
    } else {
        /* RTL:187da..18959: byte-aligned and masked planar VGA paths. */
        uint16_t copy_bytes = vfx_word(image, DOS_VFX_ROW_BYTES);
        uint8_t last_mask = (uint8_t)vfx_word(image, DOS_VFX_LAST_BYTE_MASK);
        if (gate_far_byte(image.segment,
                (uint16_t)(image.offset + DOS_VFX_PLANES)) != 4u ||
            gate_far_byte(image.segment,
                (uint16_t)(image.offset + DOS_VFX_BPP)) != 1u)
            abort();
        if (width < original_width) {
            copy_bytes = (uint16_t)((width + 7u) / 8u);
            last_mask = (uint8_t)(0xffu << ((8u - (width & 7u)) & 7u));
        }
        if ((x & 7u) == 0u && last_mask == 0xffu) {
            vfx_mode12_aligned(x, y, height, stride, copy_bytes,
                               operation, source);
        } else {
            vfx_mode12_masked(x, y, height, stride, copy_bytes,
                              last_mask, operation, source);
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
