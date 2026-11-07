/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifdef NDEBUG
#undef NDEBUG
#endif

#include "gate_exe.h"
#include "gate_rtl.h"
#include "gate_dos_host_files.h"
#include "gate_intro_host_input.h"
#include "gate_intro_host_memory.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Unused alternative text-mode original RTL calls: the intro's graphics
 * font ID 5 must not enter any of these three paths. */
void gate_rtl_f997(uint16_t x, uint16_t y)
{
    (void)x; (void)y;
    abort();
}
void gate_rtl_fa45(GateFarPtr text) { (void)text; abort(); }
void gate_rtl_f959(uint16_t *x, uint16_t *y)
{
    (void)x; (void)y;
    abort();
}

uint16_t gate_rtl_file_stack_word(void) { return 0; }
_Noreturn void gate_rtl_file_divide_fault(void) { abort(); }

typedef struct FakeClock {
    uint32_t milliseconds;
    unsigned polls;
} FakeClock;

typedef struct IntroCaption {
    uint16_t left, top, right, text_offset;
} IntroCaption;

/* These are the eighteen exact calls in original GATE.EXE:1302.  The text
 * itself remains in the private guest RAM snapshot, not in this test. */
static const IntroCaption captions[] = {
    { 10, 120, 310, 0x363b }, { 10, 100, 180, 0x37bf },
    { 10,  10, 160, 0x3899 }, { 10,  10, 310, 0x395a },
    {160,  10, 310, 0x3a65 }, {180,  10, 310, 0x3b9b },
    {180,  10, 310, 0x3c4f }, {130,  10, 310, 0x3d71 },
    {140,  10, 310, 0x3e79 }, {140,  10, 310, 0x3f3b },
    {140,  10, 310, 0x4086 }, { 10,  10, 140, 0x4198 },
    { 60, 120, 310, 0x42ae }, {140, 120, 310, 0x436c },
    {120,  10, 310, 0x4436 }, { 10, 100, 160, 0x45c4 },
    { 10,  10, 310, 0x46c5 }, { 10,  10, 310, 0x4820 }
};

static uint32_t clock_ms(void *user)
{
    FakeClock *state = user;
    state->milliseconds += 50; /* bounded deterministic test clock */
    return state->milliseconds;
}

static uint16_t no_pending_key(void *user)
{
    FakeClock *state = user;
    ++state->polls;
    return 0;
}

static uint16_t no_mouse(void *user)
{
    (void)user;
    return 0;
}

static uint16_t no_read_key(void *user)
{
    (void)user;
    abort();
}

static void configure_graphics(GateIntroMemory *memory)
{
    unsigned i;
    /* Reuse real relocated DS tables and modes from the 22-second guest,
     * but start with a clean font cache and a blank mode-13h framebuffer. */
    assert(memory->ds_segment == 0x2a7a);
    assert(gate_ds_word(0x0fce) == 3);
    assert(gate_ds_word(0x15cc) == 3);
    for (i = 0; i < 2; ++i) {
        uint16_t slot = (uint16_t)(0x54ce + i * 30u);
        gate_ds_set_word(slot, 0);
        gate_far_set_byte(memory->ds_segment, (uint16_t)(slot + 2u), 0xff);
        gate_far_set_byte(memory->ds_segment, (uint16_t)(slot + 3u), 0xff);
    }
    memset(memory->bytes + 0xa0000, 0, 320u * 200u);
    gate_ds_set_word(0x29de + 0x16, 0); /* pixel copy */
    gate_ds_set_word(0x29de + 0x18, 1); /* clipping enabled */
    gate_ds_set_word(0x29de + 0x1a, 0);
    gate_ds_set_word(0x29de + 0x1c, 0);
    gate_ds_set_word(0x29de + 0x1e, 319);
    gate_ds_set_word(0x29de + 0x20, 199);
    gate_ds_set_word(0x29de + 0x28, 0xffffu); /* simple line mask */
    gate_ds_set_word(0x29de + 0x2a, 1); /* unit line width */
    gate_ds_set_word(0x12de, 0); /* solid fill pattern */
    gate_ds_set_word(0x12e0, 0);
}

/* For inspection, use a contrasting *diagnostic* grayscale color map.
 * This is NOT the original palette, which is separate from VRAM. */
static void write_grayscale(const char *path, GateIntroMemory *memory)
{
    FILE *out = fopen(path, "wb");
    unsigned i;
    assert(out);
    assert(fprintf(out, "P5\n320 200\n255\n") > 0);
    for (i = 0; i < 320u * 200u; ++i) {
        uint8_t index = memory->bytes[0xa0000 + i];
        uint8_t value = index == 0 ? 0 : index == 1 ? 188 :
                        index == 2 ? 110 : index == 255 ? 245 : index;
        assert(fwrite(&value, 1, 1, out) == 1);
    }
    assert(fclose(out) == 0);
}

int main(int argc, char **argv)
{
    GateIntroMemory *memory = calloc(1, sizeof *memory);
    GateDosHostFiles files = {0};
    FakeClock clock = {0};
    GateIntroHostInput input = {&clock, clock_ms, no_pending_key,
                               no_read_key, no_mouse};
    GateFarPtr font;
    FILE *capture;
    unsigned nonzero = 0;
    unsigned i;
    unsigned page;
    unsigned total_glyph_pixels = 0;

    if (argc != 3 && argc != 4) {
        fputs("usage: launcher-intro-native-caption-tests guest-22s.bin DOS-DIR [output.pgm]\n",
              stderr);
        return 2;
    }
    assert(memory);
    capture = fopen(argv[1], "rb");
    assert(capture);
    assert(fread(memory->bytes, 1, GATE_INTRO_RAM_BYTES, capture) ==
           GATE_INTRO_RAM_BYTES);
    assert(fclose(capture) == 0);
    memory->ds_segment = 0x2a7a;
    gate_intro_memory_attach(memory);
    files.directory = argv[2];
    gate_dos_host_files_attach(&files);
    gate_intro_input_attach(&input);
    configure_graphics(memory);

    for (page = 0; page < sizeof captions / sizeof captions[0]; ++page) {
        const IntroCaption *caption = &captions[page];
        GateFarPtr text = {memory->ds_segment, caption->text_offset};
        uint32_t before_ms = clock.milliseconds;
        unsigned before_polls = clock.polls;
        uint16_t len = gate_far_02a5_2bba(text);
        uint16_t left = caption->left, top = caption->top;
        uint16_t right = caption->right, bottom = 0;
        uint8_t ch = gate_far_byte(text.segment, text.offset);
        unsigned page_pixels = 0;
        unsigned page_glyph_pixels = 0;
        unsigned y, x;

        assert(len > 20 && len < 500);
        memset(memory->bytes + 0xa0000, 0, 320u * 200u);
        gate_exe_117e(left, top, right, text);
        font = gate_rtl_active_font();
        assert(font.segment == memory->ds_segment && font.offset == 0x54ce);
        assert(gate_far_byte(font.segment, font.offset + 3) == 5);
        assert(gate_rtl_ebf7() == 11);
        assert(gate_dos_host_file_stream(5) == 0); /* original close */
        assert(clock.milliseconds - before_ms >=
               (uint16_t)(2000u + 45u * len));
        assert(clock.polls > before_polls);
        /* Compare the first printed glyph against independent original FNT
         * bitmap bits, for every page and bounding-box configuration.  In
         * mode 3 caption text is color 0 on a frame interior of color 1. */
        uint8_t first = gate_far_byte(font.segment, font.offset + 7);
        uint8_t row_bytes = gate_far_byte(font.segment, font.offset + 6);
        uint8_t height = gate_far_byte(font.segment, font.offset + 10);
        uint16_t width_seg = gate_far_word(font.segment, font.offset + 16);
        uint16_t width_off = gate_far_word(font.segment, font.offset + 14);
        uint16_t spacing_seg = gate_far_word(font.segment, font.offset + 20);
        uint16_t spacing_off = gate_far_word(font.segment, font.offset + 18);
        uint16_t bitmap_seg = gate_far_word(font.segment, font.offset + 28);
        uint16_t bitmap_off = gate_far_word(font.segment, font.offset + 26);
        uint8_t width = gate_far_byte(width_seg, (uint16_t)(width_off + ch));
        uint8_t spacing = gate_far_byte(spacing_seg, (uint16_t)(spacing_off + ch));
        assert(ch >= first && width > 0 && width <= 16);
        assert(gate_far_word(gate_ds_word(0x4e44), 0x4c) == 0);
        gate_exe_103a(&left, &top, &right, &bottom, text);
        assert(left == caption->left + 5 &&
               right == caption->right - 5 && bottom <= 185);
        assert(top < 200 && top + height < 200);
        for (y = 0; y < height; ++y)
            for (x = 0; x < width; ++x) {
                uint16_t source = (uint16_t)(bitmap_off +
                    (ch - first) * row_bytes * height + y * row_bytes + x / 8);
                uint8_t pixel = gate_far_byte(bitmap_seg, source);
                uint8_t bit = (uint8_t)(0x80u >> (x & 7u));
                unsigned glyph_x = left + (spacing >> 4) + x;
                unsigned position = 320u * (top + y) + glyph_x;
                assert(glyph_x < 320);
                if ((pixel & bit) != 0)
                    ++page_glyph_pixels;
                assert(memory->bytes[0xa0000 + position] ==
                       ((pixel & bit) != 0 ? 0 : 1));
            }
        assert(page_glyph_pixels >= 3);
        total_glyph_pixels += page_glyph_pixels;
        for (i = 0; i < 320u * 200u; ++i)
            page_pixels += memory->bytes[0xa0000 + i] != 0;
        assert(page_pixels > 1000);
        nonzero += page_pixels;
        if (argc == 4 && page == 0)
            write_grayscale(argv[3], memory);
    }

    assert(nonzero > 18u * 1000u);

    gate_rtl_e771();
    gate_intro_input_detach();
    gate_dos_host_files_close_all(&files);
    gate_intro_memory_detach();
    free(memory);
    printf("source-C EXE: all %zu original captions, real FNT, frame, clock: "
           "%u nonzero pixels, %u checked glyph pixels\n",
           sizeof captions / sizeof captions[0], nonzero, total_glyph_pixels);
    return 0;
}
