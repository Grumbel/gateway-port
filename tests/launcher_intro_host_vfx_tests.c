/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "gate_intro_host_memory.h"
#include "gate_rtl_image.h"
#include "gate_rtl_graphics.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned saved_calls;
static unsigned restored_calls;

uint16_t gate_far_1e48_04d9(void)
{
    ++saved_calls;
    return 0xbeefu;
}

void gate_far_1e48_03c5(uint16_t saved)
{
    assert(saved == 0xbeefu);
    ++restored_calls;
}

uint16_t gate_far_12a5_3b0c(void) { return 0; }
void gate_far_223f_00b6(uint32_t delay) { (void)delay; }
void gate_rtl_131cb(GateFarPtr work) { (void)work; }

static void set_image_word(GateFarPtr work, uint16_t offset, uint16_t value)
{
    gate_far_set_word(work.segment, (uint16_t)(work.offset + 8u + offset), value);
}

static void set_image_byte(GateFarPtr work, uint16_t offset, uint8_t value)
{
    gate_far_set_byte(work.segment, (uint16_t)(work.offset + 8u + offset), value);
}

static void make_image(GateFarPtr work, uint16_t x, uint16_t y)
{
    gate_far_set_byte(work.segment, work.offset, 1); /* active record */
    gate_far_set_byte(work.segment, (uint16_t)(work.offset + 1u), 0);
    gate_far_set_word(work.segment, (uint16_t)(work.offset + 2u), x);
    gate_far_set_word(work.segment, (uint16_t)(work.offset + 4u), y);

    set_image_word(work, 0x00, 0xca00); /* VFX object signature */
    set_image_byte(work, 0x12, 1); /* one plane */
    set_image_byte(work, 0x13, 8); /* VGA 8-bit pixels */
    set_image_word(work, 0x14, 7); /* original row stride */
    set_image_word(work, 0x18, 0xfffc); /* pixel data crosses 64KiB offset */
    set_image_word(work, 0x1a, 0x4100);
    set_image_word(work, 0x2c, 6); /* width in pixels */
    set_image_word(work, 0x2e, 3); /* height in rows */
    set_image_word(work, 0x30, 6); /* bytes per row */
}

static void fill_source(void)
{
    unsigned y;
    unsigned x;
    for (y = 0; y < 3; ++y)
        for (x = 0; x < 6; ++x)
            gate_far_set_byte(0x4100, (uint16_t)(0xfffc + 7 * y + x),
                              (uint8_t)(1u + x + 0x10u * y));
}

static void check_rect(uint16_t x, uint16_t y, unsigned width,
                       unsigned height, uint8_t previous, int operation)
{
    unsigned row;
    unsigned col;
    for (row = 0; row < height; ++row) {
        for (col = 0; col < width; ++col) {
            uint8_t source = (uint8_t)(1u + col + 0x10u * row);
            uint8_t expected;
            switch (operation) {
            case 0: expected = source; break;
            case 1: expected = (uint8_t)(previous & source); break;
            case 2: expected = (uint8_t)(previous | source); break;
            default: expected = (uint8_t)(previous ^ source); break;
            }
            assert(gate_far_byte(0xa000, (uint16_t)(320u * (y + row) + x + col))
                   == expected);
        }
    }
    assert(gate_far_byte(0xa000, (uint16_t)(320u * y + x - 1u)) == previous);
}

int main(void)
{
    GateIntroMemory *memory = calloc(1, sizeof(*memory));
    GateFarPtr work = {0x5000, 0x1100};
    unsigned operation;
    assert(memory != NULL);
    memory->ds_segment = 0x2a7a;
    gate_intro_memory_attach(memory);
    gate_far_set_word(0x2a7a, 0x15cc, 3); /* original VFX descriptor #3 */
    gate_far_set_word(0x2a7a, 0x1418, 0); /* RTL origin x */
    gate_far_set_word(0x2a7a, 0x141a, 0); /* RTL origin y */

    make_image(work, 315, 198);
    fill_source();
    for (operation = 0; operation < 4; ++operation) {
        unsigned row;
        unsigned col;
        for (row = 0; row < 200; ++row)
            for (col = 0; col < 320; ++col)
                gate_far_set_byte(0xa000, (uint16_t)(320u * row + col), 0x55);
        gate_far_set_byte(work.segment, (uint16_t)(work.offset + 1u),
                          (uint8_t)operation);
        gate_rtl_12ed7(work); /* reconstructed original C wrapper */
        check_rect(315, 198, 5, 2, 0x55, (int)operation);
        assert(gate_far_byte(0xa000, (uint16_t)(320u * 197u + 315u)) == 0x55);
        assert(gate_far_byte(0xa000, (uint16_t)(320u * 198u + 314u)) == 0x55);
    }
    assert(saved_calls == 4 && restored_calls == 4);

    /* Unclipped rows, including the source wrap at 4100:ffff -> 4100:0000. */
    make_image(work, 50, 30);
    gate_far_set_byte(work.segment, (uint16_t)(work.offset + 1u), 0);
    gate_rtl_12ed7(work);
    check_rect(50, 30, 6, 3, 0x55, 0);

    /* Invalid image signatures and offscreen placement must not write. */
    set_image_word(work, 0x00, 0);
    gate_rtl_12ed7(work);
    assert(gate_far_byte(0xa000, (uint16_t)(320u * 30u + 50u)) == 1);
    set_image_word(work, 0x00, 0xca00);
    gate_far_set_word(work.segment, (uint16_t)(work.offset + 2u), 320);
    gate_rtl_12ed7(work);
    assert(gate_far_byte(0xa000, (uint16_t)(320u * 30u + 50u)) == 1);
    gate_far_set_word(work.segment, (uint16_t)(work.offset + 2u), 50);
    gate_far_set_word(work.segment, (uint16_t)(work.offset + 4u), 200);
    gate_rtl_12ed7(work);
    assert(gate_far_byte(0xa000, (uint16_t)(320u * 30u + 50u)) == 1);
    assert(saved_calls == 8 && restored_calls == 8);

    /* Actual original 245d:00f7 image constructor + 2479:0002 fill +
     * 24a7:000c VGA draw + 245d:016c destructor, with host FAR heap. */
    {
        GateFarPtr record = {0x5100, 0x1800};
        GateFarPtr image = {record.segment, (uint16_t)(record.offset + 8u)};
        GateFarPtr second = {0x5100, 0x1a00};
        GateFarPtr secondary_image = {second.segment, (uint16_t)(second.offset + 8u)};
        uint16_t pixels_segment;
        unsigned i;

        /* 2409:00e8 mode-3 table, resident source RAM has 320x200, 8bpp,
         * one plane, and a 0x140-byte screen pitch/stride. */
        gate_far_set_byte(0x2a7a, 0x27ee + 3 * 0x3e + 0x17, 8);
        gate_far_set_byte(0x2a7a, 0x27ee + 3 * 0x3e + 0x1e, 1);
        gate_far_set_word(0x2a7a, 0x27ee + 3 * 0x3e + 0x18, 320);
        gate_far_set_word(0x2a7a, 0x27ee + 3 * 0x3e + 0x1a, 200);
        gate_far_set_word(0x2a7a, 0x27ee + 3 * 0x3e + 0x24, 0);

        assert(gate_far_145d_00f7(3, 5, 3, image, 0) == 0);
        assert(gate_far_word(image.segment, image.offset) == 0xca00);
        assert(gate_far_word(image.segment, (uint16_t)(image.offset + 0x2c)) == 5);
        assert(gate_far_word(image.segment, (uint16_t)(image.offset + 0x2e)) == 3);
        assert(gate_far_word(image.segment, (uint16_t)(image.offset + 0x14)) == 6);
        assert(gate_far_word(image.segment, (uint16_t)(image.offset + 0x30)) == 5);
        assert(gate_far_word(image.segment, (uint16_t)(image.offset + 0x32)) == 0x00ff);
        assert(gate_far_word(image.segment, (uint16_t)(image.offset + 0x34)) == 3);
        assert(gate_far_word(image.segment, (uint16_t)(image.offset + 0x18)) == 0);
        pixels_segment = gate_far_word(image.segment, (uint16_t)(image.offset + 0x1a));
        assert(pixels_segment == 0x6000);

        gate_far_1479_0002(0x00aa, image);
        for (i = 0; i < 18; ++i)
            assert(gate_far_byte(pixels_segment, (uint16_t)i) == 0xaa);

        gate_far_set_byte(record.segment, record.offset, 1);
        gate_far_set_byte(record.segment, (uint16_t)(record.offset + 1u), 0);
        gate_far_set_word(record.segment, (uint16_t)(record.offset + 2u), 25);
        gate_far_set_word(record.segment, (uint16_t)(record.offset + 4u), 37);
        gate_rtl_12ed7(record);
        for (i = 0; i < 3; ++i) {
            unsigned x;
            for (x = 0; x < 5; ++x)
                assert(gate_far_byte(0xa000,
                    (uint16_t)((37u + i) * 320u + 25u + x)) == 0xaa);
        }

        /* Resourcing two images can coexist; releasing one reuses the
         * first paragraph-aligned interval without invalidating the other. */
        assert(gate_far_145d_00f7(3, 5, 3, secondary_image, 0) == 0);
        assert(gate_far_word(secondary_image.segment,
                             (uint16_t)(secondary_image.offset + 0x1a)) != pixels_segment);
        gate_rtl_graphics_release(image);
        for (i = 0; i < 0x80; ++i)
            assert(gate_far_byte(image.segment, (uint16_t)(image.offset + i)) == 0xff);
        assert(gate_far_145d_00f7(3, 5, 3, image, 0) == 0);
        assert(gate_far_word(image.segment,
                             (uint16_t)(image.offset + 0x1a)) == pixels_segment);
        gate_rtl_graphics_release(image);
        gate_rtl_graphics_release(secondary_image);

        /* A single VGA mode-13h image never spans more than 64KiB inside
         * the segmented VFX allocator. Oversized rows are reported, not
         * wrapped into a corrupted allocation. */
        assert(gate_far_145d_00f7(200, 320, 3, image, 0) == 0);
        gate_rtl_graphics_release(image);
        assert(gate_far_145d_00f7(201, 320, 3, image, 0) == 0);
        gate_rtl_graphics_release(image);
        assert(gate_far_145d_00f7(256, 320, 3, image, 0) == 0xffe6);
    }

    gate_intro_memory_detach();
    free(memory);
    puts("original C intro wrapper -> native mode-13h VFX blit: OK");
    return 0;
}
