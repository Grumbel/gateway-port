/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "intro_source_c_full.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct TestInput {
    uint32_t now;
    uint32_t interrupt_at;
    uint32_t other_key_at;
    unsigned escape_consumed;
    unsigned other_consumed;
    unsigned published;
    unsigned planar_published;
    unsigned logo_checked;
    unsigned original_start;
    uint8_t logo_fade_levels[64];
    unsigned music_starts;
} TestInput;

static uint32_t clock_ms(void *userdata)
{
    TestInput *input = userdata;
    input->now += input->original_start ? 1 : 50;
    return input->now;
}

static void delay_ms(void *userdata, uint32_t duration)
{
    TestInput *input = userdata;
    input->now += duration;
}

static uint16_t key_pending(void *userdata)
{
    TestInput *input = userdata;
    return (input->other_key_at != 0 &&
            input->now >= input->other_key_at && !input->other_consumed) ||
           (input->now >= input->interrupt_at && !input->escape_consumed);
}

static uint16_t read_key(void *userdata)
{
    TestInput *input = userdata;
    assert(key_pending(userdata));
    if (input->other_key_at && input->now >= input->other_key_at &&
        !input->other_consumed) {
        ++input->other_consumed;
        return 'x';
    }
    ++input->escape_consumed;
    return 27;
}

static void frame_ready(void *userdata, const uint8_t *pixels,
                        const uint8_t dac[256][3])
{
    TestInput *input = userdata;
    assert(pixels && dac);
    if (input->published == 0)
        assert(pixels[0] == 0); /* host starts with a cleared framebuffer */
    ++input->published;
}

static void frame_ready_vga16(void *userdata,
                              const uint8_t planes[4][0x10000],
                              const uint8_t dac[256][3],
                              const uint8_t attributes[32])
{
    TestInput *input = userdata;
    ++input->planar_published;
    if (dac[attributes[3]][0] < 64)
        input->logo_fade_levels[dac[attributes[3]][0]] = 1;
    if (input->logo_checked || input->now < 4000)
        return;
    /* The original 4-second GATE.EXE checkpoint has a 640x480 white
     * background and exactly 3,046 black logo pixels. Check the whole
     * four-plane + attribute + DAC path, not just its screen dimensions. */
    unsigned black = 0, white = 0;
    for (unsigned y = 0; y < 480; ++y)
        for (unsigned x = 0; x < 640; ++x) {
            const unsigned offset = y * 80u + x / 8u;
            const uint8_t bit = (uint8_t)(0x80u >> (x & 7u));
            unsigned index = 0;
            for (unsigned plane = 0; plane < 4; ++plane)
                if (planes[plane][offset] & bit)
                    index |= 1u << plane;
            const uint8_t *rgb = dac[attributes[index]];
            if (rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0)
                ++black;
            else if (rgb[0] == 63 && rgb[1] == 63 && rgb[2] == 63)
                ++white;
        }
    assert(black == 3046);
    assert(white == 640u * 480u - black);
    ++input->logo_checked;
}

static void music_start(void *userdata)
{
    TestInput *input = userdata;
    assert(input->now == 0); /* original C calls common init before delays */
    ++input->music_starts;
}

int main(int argc, char **argv)
{
    TestInput input = {0};
    GateSourceCFullHooks hooks = {0};
    int result;
    if ((argc != 3 && argc != 4) ||
        (argc == 4 && strcmp(argv[3], "--original-video-start") != 0)) {
        fputs("usage: launcher-intro-source-c-live-tests snapshot.bin DOS-DIR [--original-video-start]\n", stderr);
        return 2;
    }
    if (argc == 4) {
        input.original_start = 1;
        input.interrupt_at = 25000;
        hooks.userdata = &input;
        hooks.clock_ms = clock_ms;
        hooks.delay_ms = delay_ms;
        hooks.key_pending = key_pending;
        hooks.read_key = read_key;
        hooks.frame_ready = frame_ready;
        hooks.frame_ready_vga16 = frame_ready_vga16;
        hooks.original_video_start = 1;
        hooks.music_start = music_start;
        result = gate_source_c_full_run(argv[1], argv[2], &hooks);
        assert(result == 0);
        assert(input.logo_checked == 1);
        assert(input.planar_published > 10);
        assert(input.published > 10);
        assert(input.escape_consumed == 1);
        assert(input.music_starts == 1);
        unsigned faded_levels = 0;
        for (unsigned i = 0; i < 64; ++i)
            faded_levels += input.logo_fade_levels[i];
        assert(faded_levels > 8); /* intermediate DAC fade frames, not just endpoints */
        printf("original VGA 12h logo + 13h transition: planar=%u indexed=%u, time=%u ms\n",
               input.planar_published, input.published, input.now);
        return 0;
    }
    input.interrupt_at = 65000;
    hooks.userdata = &input;
    hooks.clock_ms = clock_ms;
    hooks.delay_ms = delay_ms;
    hooks.key_pending = key_pending;
    hooks.read_key = read_key;
    hooks.frame_ready = frame_ready;
    hooks.music_start = music_start;
    result = gate_source_c_full_run(argv[1], argv[2], &hooks);
    assert(result == 0);
    assert(input.escape_consumed == 1);
    assert(input.other_consumed == 0);
    assert(input.published > 10);
    assert(input.music_starts == 1);
    assert(input.now >= input.interrupt_at && input.now < 100000);
    printf("original C Escape: time=%u ms, published=%u, keys=%u\n",
           input.now, input.published, input.escape_consumed);
    input = (TestInput){0};
    input.other_key_at = 50000;
    input.interrupt_at = 100000;
    result = gate_source_c_full_run(argv[1], argv[2], &hooks);
    assert(result == 0);
    assert(input.other_consumed == 1);
    assert(input.escape_consumed == 1);
    assert(input.published > 10);
    assert(input.music_starts == 1);
    printf("original C other-key + Escape: time=%u ms, frames=%u\n",
           input.now, input.published);
    return 0;
}
