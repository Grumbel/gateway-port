/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifdef NDEBUG
#undef NDEBUG
#endif

#include "gate_exe.h"

#include <assert.h>
#include <stdio.h>

static uint16_t buffered;
static uint16_t keys[12];
static unsigned key_count, key_index, polls, reads, stores;

static void reset(void)
{
    unsigned i;
    buffered = 0xffff;
    key_count = key_index = polls = reads = stores = 0;
    for (i = 0; i < sizeof keys / sizeof keys[0]; ++i)
        keys[i] = 0;
}

uint16_t gate_far_0df2_000a(void)
{
    ++polls;
    return buffered != 0xffff || key_index < key_count;
}

uint16_t gate_far_0df2_0030(void)
{
    ++reads;
    if (buffered != 0xffff) {
        uint16_t result = buffered;
        buffered = 0xffff;
        return result;
    }
    assert(key_index < key_count);
    return keys[key_index++];
}

void gate_ds_set_word(uint16_t offset, uint16_t value)
{
    assert(offset == 0x1340);
    ++stores;
    buffered = value;
}

static void test_conditional_unget(void)
{
    reset();
    assert(gate_far_0df2_00f8() == 0);
    assert(polls == 1 && reads == 0 && stores == 0);

    reset();
    keys[0] = ' ';
    keys[1] = '\r';
    keys[2] = 27;
    key_count = 3;
    assert(gate_far_0df2_00f8() == ' ');
    assert(gate_far_0df2_00f8() == '\r');
    assert(stores == 0 && reads == 2);
    assert(gate_far_0df2_00f8() == 27);
    assert(stores == 1 && reads == 3 && buffered == 27);
    assert(gate_far_0df2_000a() != 0); /* key available again */
    assert(gate_far_0df2_0030() == 27);
    assert(buffered == 0xffff);

    reset();
    keys[0] = 3;
    key_count = 1;
    assert(gate_far_0df2_00f8() == 3);
    assert(buffered == 3); /* Ctrl-C is also kept */
}

static void test_drain_pending_keys(void)
{
    reset();
    gate_far_0df2_0128();
    assert(polls == 1 && reads == 0);

    reset();
    keys[0] = 0x12;
    keys[1] = 0x1b;
    keys[2] = 0x103; /* original reads full 16-bit key code */
    key_count = 3;
    buffered = 0x78;
    gate_far_0df2_0128();
    assert(key_index == 3 && buffered == 0xffff);
    assert(reads == 4 && polls == 5 && stores == 0);
}

int main(void)
{
    test_conditional_unget();
    test_drain_pending_keys();
    puts("linked original DOS intro keyboard: buffered-key and flush passed");
    return 0;
}
