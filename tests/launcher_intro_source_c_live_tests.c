/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "intro_source_c_full.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

typedef struct TestInput {
    uint32_t now;
    uint32_t interrupt_at;
    uint32_t other_key_at;
    unsigned escape_consumed;
    unsigned other_consumed;
    unsigned published;
} TestInput;

static uint32_t clock_ms(void *userdata)
{
    TestInput *input = userdata;
    input->now += 50;
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

int main(int argc, char **argv)
{
    TestInput input = {0};
    GateSourceCFullHooks hooks;
    int result;
    if (argc != 3) {
        fputs("usage: launcher-intro-source-c-live-tests snapshot.bin DOS-DIR\n", stderr);
        return 2;
    }
    input.interrupt_at = 65000;
    hooks.userdata = &input;
    hooks.clock_ms = clock_ms;
    hooks.delay_ms = delay_ms;
    hooks.key_pending = key_pending;
    hooks.read_key = read_key;
    hooks.frame_ready = frame_ready;
    result = gate_source_c_full_run(argv[1], argv[2], &hooks);
    assert(result == 0);
    assert(input.escape_consumed == 1);
    assert(input.other_consumed == 0);
    assert(input.published > 10);
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
    printf("original C other-key + Escape: time=%u ms, frames=%u\n",
           input.now, input.published);
    return 0;
}
