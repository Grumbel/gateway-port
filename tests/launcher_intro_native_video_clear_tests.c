/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifdef NDEBUG
#undef NDEBUG
#endif

#include "gate_intro_host_memory.h"
#include "gate_rtl_video.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t block_mode;
static unsigned cursor_draws;

uint16_t gate_rtl_video_block_mode(void) { return block_mode; }
uint8_t gate_rtl_video_attribute(void) { return 7; }
void gate_rtl_video_command(uint16_t function, uint8_t *input, uint8_t *output)
{
    assert(function == 0x10 && input == output);
    assert(input[0] == 0xdb && input[1] == 9 && input[2] == 0x87);
    ++cursor_draws;
}

static void descriptor(unsigned mode, unsigned height, unsigned pitch,
                       unsigned planes, uint16_t aperture, uint16_t stride)
{
    uint16_t ds = gate_ds_segment();
    uint16_t at = (uint16_t)(0x27eeu + mode * 0x3eu);
    gate_far_set_byte(ds, at, (uint8_t)mode);
    gate_far_set_word(ds, (uint16_t)(at + 0x1a), (uint16_t)height);
    gate_far_set_word(ds, (uint16_t)(at + 0x1c), (uint16_t)pitch);
    gate_far_set_byte(ds, (uint16_t)(at + 0x1e), (uint8_t)planes);
    gate_far_set_word(ds, (uint16_t)(at + 0x20), aperture);
    gate_far_set_word(ds, (uint16_t)(at + 0x22), stride);
}

static void assert_aperture(const uint8_t *bytes, size_t first, size_t length,
                            uint8_t expected)
{
    size_t i;
    for (i = first; i < first + length; ++i)
        assert(bytes[i] == expected);
}

static void test_synthetic(GateIntroMemory *memory)
{
    unsigned p;
    memset(memory, 0, sizeof *memory);
    memory->ds_segment = 0x2a7a;
    descriptor(0, 200, 80, 1, 0xb800, 0);
    descriptor(1, 350, 80, 4, 0xa000, 0x8000);
    descriptor(2, 480, 80, 4, 0xa000, 0);
    descriptor(3, 200, 320, 1, 0xa000, 0);
    descriptor(7, 600, 100, 4, 0xa000, 0);

    gate_ds_set_word(0x15cc, 3);
    memset(memory->bytes + 0xa0000, 0xaa, 65536);
    assert(gate_far_2488_000a(0, 0x1234) == 0);
    assert_aperture(memory->bytes, 0xa0000, 64000, 0x34);
    assert_aperture(memory->bytes, 0xa0000 + 64000, 1536, 0xaa);
    assert(gate_far_byte(0x2a7a, 0x27ed) == 1);
    assert(gate_far_word(0x2a7a, 0x27ee + 3 * 0x3e + 0x3a) == 0x01d7);
    assert(gate_far_word(0x2a7a, 0x27ee + 2 * 0x3e + 0x3a) == 0x0169);
    assert(gate_far_word(0x2a7a, 0x27ee + 0 * 0x3e + 0x3a) == 0x011a);

    block_mode = 0;
    gate_rtl_f921(); /* actual GATE.RTL source C -> resident native host */
    assert_aperture(memory->bytes, 0xa0000, 64000, 0);
    assert_aperture(memory->bytes, 0xa0000 + 64000, 1536, 0xaa);
    assert(cursor_draws == 0);
    block_mode = 1;
    gate_rtl_f921();
    assert(cursor_draws == 1);
    block_mode = 0;

    for (p = 0; p < 4; ++p)
        memset(memory->vga_planes[p], 0xa5, 65536);
    gate_ds_set_word(0x15cc, 2);
    assert(gate_far_2488_000a(0, 0x000d) == 0);
    for (p = 0; p < 4; ++p) {
        assert_aperture(memory->vga_planes[p], 0, 38400,
                        (0x0d & (1u << p)) ? 0xff : 0);
        assert_aperture(memory->vga_planes[p], 38400, 27136, 0xa5);
    }
    assert(gate_intro_vga16_pixel(0, 0) == 13);
    assert(gate_intro_vga16_pixel(639, 479) == 13);

    /* Mode 1 has a second display page at physical planar offset 8000h. */
    gate_ds_set_word(0x15cc, 1);
    assert(gate_far_2488_000a(1, 0x0002) == 0);
    for (p = 0; p < 4; ++p) {
        assert_aperture(memory->vga_planes[p], 0, 32768,
                        (0x0d & (1u << p)) ? 0xff : 0);
        assert_aperture(memory->vga_planes[p], 32768, 28000,
                        p == 1 ? 0xff : 0);
    }

    /* Modes 4..7 dispatch to the same planar callback, including 800x600. */
    gate_ds_set_word(0x15cc, 7);
    assert(gate_far_2488_000a(0, 0x0009) == 0);
    for (p = 0; p < 4; ++p) {
        assert_aperture(memory->vga_planes[p], 0, 60000,
                        (0x09 & (1u << p)) ? 0xff : 0);
        assert_aperture(memory->vga_planes[p], 60768, 65536 - 60768,
                        0xa5);
    }

    gate_ds_set_word(0x15cc, 0);
    memset(memory->bytes + 0xb8000, 0xaa, 16385);
    assert(gate_far_2488_000a(0, 1) == 0);
    assert_aperture(memory->bytes, 0xb8000, 16384, 0xff);
    assert(memory->bytes[0xb8000 + 16384] == 0xaa);
    assert(gate_far_2488_000a(0, 0) == 0);
    assert_aperture(memory->bytes, 0xb8000, 16384, 0);

    gate_ds_set_word(0x15cc, 8);
    assert(gate_far_2488_000a(0, 0) == 0xfffa);
    gate_ds_set_word(0x15cc, 3);
    gate_far_set_byte(0x2a7a, (uint16_t)(0x27ee + 3 * 0x3e), 2);
    assert(gate_far_2488_000a(0, 0) == 0xfc19);
}

static void test_original_snapshot(GateIntroMemory *memory, const char *path)
{
    FILE *file = fopen(path, "rb");
    uint8_t *original = malloc(GATE_INTRO_RAM_BYTES);
    assert(file && original);
    assert(fread(memory->bytes, 1, GATE_INTRO_RAM_BYTES, file) ==
           GATE_INTRO_RAM_BYTES);
    assert(fgetc(file) == EOF);
    fclose(file);
    memory->ds_segment = 0x2a7a;
    assert(gate_ds_word(0x15cc) == 3);
    assert(gate_far_byte(0x2a7a, 0x27ed) == 1);
    memcpy(original, memory->bytes, GATE_INTRO_RAM_BYTES);
    memset(memory->bytes + 0xa0000, 0x55, 65536);
    block_mode = 0;
    gate_rtl_f921();
    assert_aperture(memory->bytes, 0xa0000, 64000, 0);
    assert_aperture(memory->bytes, 0xa0000 + 64000, 1536, 0x55);
    /* No guest memory outside the destination aperture is modified. */
    assert(memcmp(memory->bytes, original, 0xa0000) == 0);
    assert(memcmp(memory->bytes + 0xb0000, original + 0xb0000,
                  GATE_INTRO_RAM_BYTES - 0xb0000) == 0);
    free(original);
}

int main(int argc, char **argv)
{
    GateIntroMemory *memory = calloc(1, sizeof *memory);
    assert(memory);
    gate_intro_memory_attach(memory);
    test_synthetic(memory);
    if (argc > 1)
        test_original_snapshot(memory, argv[1]);
    gate_intro_memory_detach();
    free(memory);
    puts("resident display clear: text, linear, planar, page stride, RTL: OK");
    return 0;
}
