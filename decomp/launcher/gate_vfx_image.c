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

/* 145d:00f7 -> resident 245d:00f7: VFX image allocation, backed by
 * 244c:00b9 (row-stride/plane-count size) and 245d:0006 (CA00 record
 * constructor). Recovered for video mode 3, whose resident descriptor is
 * one plane of eight-bit pixels. Other mode descriptor types have different
 * bit masks and storage layouts and are deliberately not emulated here.
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
    GateFarPtr pixels;

    if (mode != 3)
        abort(); /* other original VFX pixel formats remain unresolved */
    if (width == 0 || height == 0)
        return 0xffe6u;

    /* Resident mode descriptors: 2409:00e8 -> DS:27ee+mode*0x3e.
     * Mode 3 is 320x200 8bpp, one plane. */
    descriptor = (uint16_t)(0x27eeu + 3u * 0x3eu);
    mode_width = gate_ds_word((uint16_t)(descriptor + 0x18u));
    mode_height = gate_ds_word((uint16_t)(descriptor + 0x1au));
    if (mode_width != 320 || mode_height != 200 ||
        gate_far_byte(gate_ds_segment(), (uint16_t)(descriptor + 0x17u)) != 8 ||
        gate_far_byte(gate_ds_segment(), (uint16_t)(descriptor + 0x1eu)) != 1)
        abort(); /* original VFX mode table has not been initialized */

    row_bytes = width;
    stride = (uint16_t)((row_bytes + 1u) & ~1u);
    length = (uint32_t)stride * height;
    if (length > 0xffffu)
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
    set_image_byte(image, 0x12, 1);
    set_image_byte(image, 0x13, 8);
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
    set_image_word(image, 0x32, 0x00ffu);
    set_image_word(image, 0x34, height);
    return 0;
}

/* 1479:0002 -> resident 2479:0002, fill the allocated image pixels.
 * The one-plane 8bpp path repeats the low-byte fill value for each byte
 * in each even-padded scanline. The original 16-bit segment-offset writer
 * advances the segment on wrap; the native single-allocation length is
 * constrained to <= 65535, and its physical allocation begins at offset 0. */
void gate_far_1479_0002(uint16_t pattern, GateFarPtr image)
{
    uint16_t row;
    uint16_t index;
    uint16_t stride;
    uint16_t height;
    GateFarPtr data;
    if (image_word(image, 0x00) != 0xca00u)
        return;
    if (gate_far_byte(image.segment, (uint16_t)(image.offset + 0x12u)) != 1 ||
        gate_far_byte(image.segment, (uint16_t)(image.offset + 0x13u)) != 8)
        abort();
    stride = image_word(image, 0x14);
    height = image_word(image, 0x34);
    data.offset = image_word(image, 0x18);
    data.segment = image_word(image, 0x1a);
    for (row = 0; row < height; ++row)
        for (index = 0; index < stride; ++index)
            gate_far_set_byte(data.segment,
                (uint16_t)(data.offset + (uint16_t)(row * stride + index)),
                (uint8_t)pattern);
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
