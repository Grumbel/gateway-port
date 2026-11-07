/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_rtl_image.h"
#include "gate_intro_host_memory.h"

#include <stdlib.h>

/* Bounded native implementation of the original linked 24a7:000c VFX
 * bitblitter's VGA mode-12h four-plane handler (24a7:02fe) and 320x200,
 * eight-bit mode-13h handler (24a7:04c4).
 *
 * The original handler operates on an already allocated/decompressed 0xca00
 * image object, NOT a GATE_*.PIC archive entry. It writes directly to VGA
 * segment a000 (mode 13h) or four VGA hardware planes (mode 12h), preserving
 * the four original raster operations. The planar handler's CPU I/O setup
 * uses the VGA sequencer map mask and GC bitmask to draw single bits; the
 * native host stores the resulting four-plane palette indices directly.
 * Remaining original video modes are deliberately rejected.
 *
 * Source evidence: 22-second guest RAM, physical 0x24a7c..0x24b8f
 * (24a7:000c), 0x24d6e..0x24f31 (24a7:02fe), 0x24f34..0x24fc2
 * (24a7:04c4), and coordinate resolvers 252c:0032 / 252c:0053. */

/* Original 244c:0000 advances the source segment on 64KiB rollover, but
 * ordinary 8086 segment:offset reads do not. The source row length can
 * exceed a segment in mode 12h (640x480x4 planes = 153600 bytes). */
static uint8_t source_byte(GateFarPtr data, uint32_t offset)
{
    uint32_t linear = (uint32_t)data.offset + offset;
    return gate_far_byte((uint16_t)(data.segment + ((linear >> 16) << 12)),
                         (uint16_t)linear);
}

static void draw_mode2(uint16_t y, uint16_t x, uint16_t signed_mode,
                       GateFarPtr image)
{
    uint16_t width;
    uint16_t height;
    uint16_t stride;
    uint16_t source_offset;
    uint16_t source_segment;
    int16_t available;
    uint16_t row;
    uint16_t col;
    uint16_t planes;

    /* Resident descriptor #2 is 640x480 with four 1-bit planes, arranged
     * as plane 0,1,2,3 for each source scanline. GC register 3 chooses
     * copy/AND/OR/XOR; the VGA sequencer selects the current plane. */
    planes = gate_far_byte(image.segment, (uint16_t)(image.offset + GATE_VFX_IMAGE_PLANES));
    if (planes != 4 ||
        gate_far_byte(image.segment, (uint16_t)(image.offset + GATE_VFX_IMAGE_BITS_PER_PLANE)) != 1)
        abort();

    available = (int16_t)(uint16_t)(640u - x);
    if (available <= 0)
        return;
    width = gate_far_word(image.segment, (uint16_t)(image.offset + GATE_VFX_IMAGE_WIDTH));
    if ((int16_t)width > available)
        width = (uint16_t)available;
    available = (int16_t)(uint16_t)(480u - y);
    if (available <= 0)
        return;
    height = gate_far_word(image.segment, (uint16_t)(image.offset + GATE_VFX_IMAGE_HEIGHT));
    if ((int16_t)height > available)
        height = (uint16_t)available;
    if (width == 0 || height == 0)
        return;

    stride = gate_far_word(image.segment, (uint16_t)(image.offset + GATE_VFX_IMAGE_ROW_STRIDE));
    source_offset = gate_far_word(image.segment, (uint16_t)(image.offset + GATE_VFX_IMAGE_PIXELS_OFFSET));
    source_segment = gate_far_word(image.segment, (uint16_t)(image.offset + GATE_VFX_IMAGE_PIXELS_SEGMENT));
    for (row = 0; row < height; ++row) {
        for (col = 0; col < width; ++col) {
            uint8_t pixel = 0;
            uint8_t current;
            uint32_t offset = (uint32_t)row * planes * stride + col / 8u;
            uint8_t bit = (uint8_t)(0x80u >> (col & 7u));
            uint16_t plane;
            for (plane = 0; plane < planes; ++plane) {
                if (source_byte((GateFarPtr){source_segment, source_offset},
                                offset + (uint32_t)plane * stride) & bit)
                    pixel |= (uint8_t)(1u << plane);
            }
            current = gate_intro_vga16_pixel((uint16_t)(x + col),
                                             (uint16_t)(y + row));
            switch (signed_mode) {
            case 0: break; /* direct replacement */
            case 3: pixel ^= current; break;
            case 1: pixel &= current; break;
            default: pixel |= current; break;
            }
            gate_intro_vga16_set_pixel((uint16_t)(x + col),
                                       (uint16_t)(y + row), pixel);
        }
    }
}

void gate_linked_blit_vfx_picture(uint16_t unused, uint16_t y, uint16_t x,
                        uint16_t signed_mode, GateFarPtr image)
{
    uint16_t count;
    uint16_t rows;
    uint16_t stride;
    uint16_t source_offset;
    uint16_t source_segment;
    uint16_t destination;
    int16_t available;
    uint16_t i;
    uint16_t row;

    (void)unused; /* original caller supplies the image origin (0,0) */

    if (gate_ds_word(GATE_DS_RENDERER_MODE) != 2 && gate_ds_word(GATE_DS_RENDERER_MODE) != 3)
        abort(); /* other original VFX mode handlers remain unresolved */

    /* Original 244c:0025 validates the CA00 signature before drawing. */
    if (gate_far_word(image.segment, image.offset) != GATE_VFX_IMAGE_SIGNATURE_VALUE)
        return;

    if (gate_ds_word(GATE_DS_RENDERER_MODE) == 2) {
        draw_mode2(y, x, signed_mode, image);
        gate_intro_memory_notify_frame();
        return;
    }

    /* In mode 3 the original descriptor is 320x200, eight bits per pixel,
     * with one image plane. Reject a malformed/cross-mode object rather than
     * interpreting its packed data as an eight-bit bitmap. */
    if (gate_far_byte(image.segment, (uint16_t)(image.offset + GATE_VFX_IMAGE_PLANES)) != 1 ||
        gate_far_byte(image.segment, (uint16_t)(image.offset + GATE_VFX_IMAGE_BITS_PER_PLANE)) != 8)
        abort();

    /* Original signed JG/JGE horizontal/vertical clipping at 24a7:009c
     * and 24a7:00dc. There is intentionally no left/top clipping: the
     * original coordinate resolver performs unsigned 16-bit arithmetic. */
    available = (int16_t)(uint16_t)(320u - x);
    if (available <= 0)
        return;
    count = gate_far_word(image.segment, (uint16_t)(image.offset + GATE_VFX_IMAGE_ROW_BYTES));
    if (available < (int16_t)gate_far_word(image.segment,
                                          (uint16_t)(image.offset + GATE_VFX_IMAGE_WIDTH)))
        count = (uint16_t)available; /* 8-bit pixels = one byte each */

    available = (int16_t)(uint16_t)(200u - y);
    if (available <= 0)
        return;
    rows = gate_far_word(image.segment, (uint16_t)(image.offset + GATE_VFX_IMAGE_HEIGHT));
    if (available < (int16_t)rows)
        rows = (uint16_t)available;
    if (count == 0 || rows == 0)
        return;

    stride = gate_far_word(image.segment, (uint16_t)(image.offset + GATE_VFX_IMAGE_ROW_STRIDE));
    source_offset = gate_far_word(image.segment, (uint16_t)(image.offset + GATE_VFX_IMAGE_PIXELS_OFFSET));
    source_segment = gate_far_word(image.segment, (uint16_t)(image.offset + GATE_VFX_IMAGE_PIXELS_SEGMENT));

    /* 252c:0053 maps VGA mode 13h (x,y) to A000:(320*y+x). */
    destination = (uint16_t)(320u * y + x);
    for (row = 0; row < rows; ++row) {
        for (i = 0; i < count; ++i) {
            const uint16_t src = (uint16_t)(source_offset + i);
            const uint16_t dst = (uint16_t)(destination + i);
            const uint8_t pixel = gate_far_byte(source_segment, src);
            const uint8_t old = gate_far_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT, dst);
            uint8_t result;
            switch (signed_mode) {
            case 0: result = pixel; break;
            case 3: result = (uint8_t)(old ^ pixel); break;
            case 1: result = (uint8_t)(old & pixel); break;
            default: result = (uint8_t)(old | pixel); break;
            }
            gate_far_set_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT, dst, result);
        }
        /* The 8086 loop adds (stride - copied) after LODS/REP MOVS;
         * destination likewise adds (320 - copied). Both net +stride
         * and +320, with the original offset-only wraparound. */
        source_offset = (uint16_t)(source_offset + stride);
        destination = (uint16_t)(destination + 320u);
    }
    gate_intro_memory_notify_frame();
}
