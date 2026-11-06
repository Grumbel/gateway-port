/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gate_intro_host_input.h"
#include "gate_exe.h"

#include <assert.h>
#include <stdio.h>

uint32_t gate_far_12a5_38c4(void);

typedef struct FakeInput {
    uint32_t ms;
    uint16_t key;
    uint16_t mouse;
} FakeInput;

static uint32_t clock_ms(void *ctx)
{
    FakeInput *input = ctx;
    return input->ms++;
}

static uint16_t key_pending(void *ctx)
{
    return ((FakeInput *)ctx)->key != 0;
}

static uint16_t read_key(void *ctx)
{
    FakeInput *input = ctx;
    uint16_t result = input->key;
    input->key = 0;
    return result;
}

static uint16_t mouse_flags(void *ctx)
{
    return ((FakeInput *)ctx)->mouse;
}

int main(void)
{
    FakeInput fake = {900, 27, 2};
    GateIntroHostInput host = {&fake, clock_ms, key_pending, read_key,
                               mouse_flags};
    gate_intro_input_attach(&host);
    assert(gate_far_02a5_38c4() == 900);
    assert(gate_far_12a5_38c4() == 901);
    assert(gate_far_0df2_000a() == 1);
    assert(gate_far_0df2_0030() == 27);
    assert(gate_far_0df2_000a() == 0);
    assert(gate_far_0e48_07fe() == 2);
    gate_intro_input_detach();
    puts("native injected intro time/key/mouse services: OK");
    return 0;
}
