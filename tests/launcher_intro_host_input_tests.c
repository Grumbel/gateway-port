/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gate_intro_host_input.h"
#include "gate_intro_host_memory.h"
#include "gate_exe.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

uint32_t gate_linked_animation_elapsed_milliseconds(void);

typedef struct FakeInput {
    uint32_t ms;
    uint16_t key;
    uint16_t mouse;
    unsigned reads;
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
    ++input->reads;
    input->key = 0;
    return result;
}

static uint16_t mouse_flags(void *ctx)
{
    return ((FakeInput *)ctx)->mouse;
}

int main(void)
{
    GateIntroMemory *memory = calloc(1, sizeof *memory);
    FakeInput fake = {900, 27, 2, 0};
    GateIntroHostInput host = {&fake, clock_ms, key_pending, read_key,
                               mouse_flags};
    assert(memory);
    memory->ds_segment = 0x3000;
    gate_intro_memory_attach(memory);
    gate_ds_set_word(0x1340, 0xffff);
    gate_intro_input_attach(&host);
    assert(gate_linked_exe_elapsed_milliseconds() == 900);
    assert(gate_linked_animation_elapsed_milliseconds() == 901);
    assert(gate_linked_keyboard_pending() == 1);
    assert(gate_linked_read_keyboard_key() == 27);
    assert(gate_linked_keyboard_pending() == 0);
    assert(gate_linked_intro_mouse_flags() == 2);

    /* Original 0df2:00f8 must requeue an ordinary non-Space/non-CR key
     * into the same DS slot read by the host's 0df2:0030 adapter. */
    fake.key = 27;
    assert(gate_linked_read_buffered_key() == 27);
    assert(gate_ds_word(0x1340) == 27);
    assert(gate_linked_keyboard_pending() != 0);
    assert(gate_linked_read_keyboard_key() == 27);
    assert(gate_ds_word(0x1340) == 0xffff);
    assert(gate_linked_keyboard_pending() == 0);

    fake.key = ' ';
    assert(gate_linked_read_buffered_key() == ' ');
    assert(gate_ds_word(0x1340) == 0xffff);
    fake.key = '\r';
    assert(gate_linked_read_buffered_key() == '\r');
    assert(gate_ds_word(0x1340) == 0xffff);

    /* Cleanup discards the requeued key and the subsequent host key. */
    gate_ds_set_word(0x1340, 42);
    fake.key = 65;
    unsigned before = fake.reads;
    gate_linked_flush_keyboard();
    assert(fake.reads == before + 1);
    assert(fake.key == 0 && gate_ds_word(0x1340) == 0xffff);
    assert(gate_linked_keyboard_pending() == 0);

    gate_intro_input_detach();
    gate_intro_memory_detach();
    free(memory);
    puts("native injected intro time/key/mouse plus source-C unget/flush: OK");
    return 0;
}
