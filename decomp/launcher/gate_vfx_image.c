/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_rtl_image.h"
#include "gate_rtl_graphics.h"

#include <stdint.h>
#include <stdlib.h>

/* Original far allocation services at 253a:0004/0048, supplied by the
 * host's conventional-memory arena in native builds. */
GateFarPtr gate_far_253a_0004(uint32_t bytes);
uint16_t gate_far_253a_0048(GateFarPtr pixels);

static uint16_t image_word(GateFarPtr image, uint16_t at)
{
    return gate_far_word(image.segment, (uint16_t)(image.offset + at));
}

static void set_image_word(GateFarPtr image, uint16_t at, uint16_t value)
{
    gate_far_set_word(image.segment, (uint16_t)(image.offset + at), value);
}

static void set_image_byte(GateFarPtr image, uint16_t at, uint8_t value)
{
    gate_far_set_byte(image.segment, (uint16_t)(image.offset + at), value);
}

/* Pixel memory may cross a segment boundary. Original 2479:0002 uses
 * 244c:0000 to advance the segment by 1000h on a 16-bit offset wrap.
 * Keep that behavior separate from ordinary FAR accesses, which wrap their
 * offset without adjusting the segment. */
static void set_pixel_byte(GateFarPtr pixels, uint32_t at, uint8_t value)
{
    uint32_t offset = (uint32_t)pixels.offset + at;
    gate_far_set_byte((uint16_t)(pixels.segment + ((offset >> 16) << 12)),
                      (uint16_t)offset, value);
}

/* 145d:00f7 -> resident 245d:00f7: VFX image allocation, backed by
 * 244c:00b9 (row-stride/plane-count size) and 245d:0006 (CA00 record
 * constructor). Mode 2 is 640x480, one bit per plane and four planes;
 * mode 3 is 320x200, eight bits per pixel and one plane. Both descriptor
 * layouts are pinned against original RAM, including their 32-bit buffer
 * lengths and even-byte alignment of each plane-row.
 * Original stack order: height, width, mode, FAR destination, flag. */
uint16_t gate_far_145d_00f7(uint16_t height, uint16_t width,
                            uint16_t mode, GateFarPtr image,
                            uint16_t zero_flag)
{
    uint32_t length;
    uint16_t row_bytes;
    uint16_t stride;
    uint16_t descriptor;
    uint16_t mode_width;
    uint16_t mode_height;
    uint16_t bpp;
    uint16_t planes;
    uint16_t remainder;
    uint8_t high_mask;
    GateFarPtr pixels;

    if (mode != 2 && mode != 3)
        abort(); /* remaining original VFX formats are unresolved */
    if (width == 0 || height == 0)
        return 0xffe6u;

    /* Resident mode descriptors: 2409:00e8 -> DS:27ee+mode*0x3e. */
    descriptor = (uint16_t)(0x27eeu + mode * 0x3eu);
    mode_width = gate_ds_word((uint16_t)(descriptor + 0x18u));
    mode_height = gate_ds_word((uint16_t)(descriptor + 0x1au));
    bpp = gate_far_byte(gate_ds_segment(), (uint16_t)(descriptor + 0x17u));
    planes = gate_far_byte(gate_ds_segment(), (uint16_t)(descriptor + 0x1eu));
    if (!((mode == 2 && mode_width == 640 && mode_height == 480 &&
           bpp == 1 && planes == 4) ||
          (mode == 3 && mode_width == 320 && mode_height == 200 &&
           bpp == 8 && planes == 1)))
        abort(); /* original VFX mode table has not been initialized */

    row_bytes = (uint16_t)(((uint32_t)width * bpp + 7u) / 8u);
    stride = (uint16_t)((row_bytes + 1u) & ~1u);
    length = (uint32_t)stride * height * planes;
    /* The original size calculator allows up to a megabyte through DX<16;
     * our isolated conventional-memory heap is deliberately smaller. */
    if (length > 0xfffffu)
        return 0xffe6u;
    pixels = gate_far_253a_0004(length);
    if (pixels.segment == 0)
        return 0xffe6u;

    /* Exact resident 245d:0006 header fields, including reserved words.
     * The caller owns the 0x80-byte record at image; the allocator owns
     * only its separate pixel region, released by 245d:016c. */
    set_image_word(image, 0x00, 0xca00u);
    set_image_byte(image, 0x02, 0x65);
    set_image_byte(image, 0x03, 0);
    set_image_word(image, 0x04, mode);
    set_image_word(image, 0x06, 0);
    set_image_word(image, 0x08, 0);
    set_image_word(image, 0x0a, (uint16_t)(width - 1u));
    set_image_word(image, 0x0c, (uint16_t)(height - 1u));
    set_image_word(image, 0x0e, mode_width);
    set_image_word(image, 0x10, mode_height);
    set_image_byte(image, 0x12, (uint8_t)planes);
    set_image_byte(image, 0x13, (uint8_t)bpp);
    set_image_word(image, 0x14, stride);
    set_image_word(image, 0x16, zero_flag);
    set_image_word(image, 0x18, pixels.offset);
    set_image_word(image, 0x1a, pixels.segment);
    set_image_word(image, 0x1c, gate_ds_word((uint16_t)(descriptor + 0x24u)));
    set_image_word(image, 0x1e, 0);
    set_image_word(image, 0x20, 0);
    set_image_word(image, 0x2c, width);
    set_image_word(image, 0x2e, height);
    set_image_word(image, 0x30, row_bytes);
    remainder = (uint16_t)(((uint32_t)width * bpp) & 7u);
    high_mask = (uint8_t)(0xffu << ((8u - remainder) & 7u));
    set_image_word(image, 0x32, (uint16_t)((uint16_t)(uint8_t)~high_mask << 8 |
                                           high_mask));
    set_image_word(image, 0x34, (uint16_t)(height * planes));
    return 0;
}

/* 1479:0002 -> resident 2479:0002, fill the allocated image pixels.
 * Its multi-plane path repeats one 0xff or 0x00 byte per plane according
 * to the corresponding bit of the caller's WORD pattern, interleaving
 * planes within each image scanline. The single-plane 8bpp path repeats
 * the low byte. The original advances the FAR segment on offset wrap. */
void gate_far_1479_0002(uint16_t pattern, GateFarPtr image)
{
    uint32_t row;
    uint32_t index;
    uint16_t stride;
    uint16_t height;
    uint8_t planes;
    uint8_t bpp;
    GateFarPtr data;
    if (image_word(image, 0x00) != 0xca00u)
        return;
    planes = gate_far_byte(image.segment, (uint16_t)(image.offset + 0x12u));
    bpp = gate_far_byte(image.segment, (uint16_t)(image.offset + 0x13u));
    if (!((planes == 1 && bpp == 8) || (planes == 4 && bpp == 1)))
        abort();
    stride = image_word(image, 0x14);
    height = image_word(image, 0x34);
    data.offset = image_word(image, 0x18);
    data.segment = image_word(image, 0x1a);
    /* +34 is already height * plane_count (245d:0006), rather than the
     * logical height in +2e. Original 2479:0002 uses it directly. */
    for (row = 0; row < height; ++row) {
        uint8_t fill = planes == 1 ? (uint8_t)pattern :
            (pattern & (1u << (row % planes))) != 0 ? 0xffu : 0;
        for (index = 0; index < stride; ++index)
            set_pixel_byte(data, row * stride + index, fill);
    }
}

/* 145d:016c -> resident 245d:016c, original CA00 resource destructor.
 * The original clears all 128 bytes of the passed image record to 0xff
 * after the FAR heap free succeeds. */
void gate_rtl_graphics_release(GateFarPtr image)
{
    GateFarPtr data;
    uint16_t i;
    if (image_word(image, 0x00) != 0xca00u)
        return;
    data.offset = image_word(image, 0x18);
    data.segment = image_word(image, 0x1a);
    if (gate_far_253a_0048(data) != 0)
        return;
    for (i = 0; i < 0x80u; ++i)
        set_image_byte(image, i, 0xffu);
}
