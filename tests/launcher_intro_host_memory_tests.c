/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gate_exe.h"
#include "gate_intro_host_memory.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_real_mode_aliasing_and_ds(void)
{
    GateIntroMemory *memory = calloc(1, sizeof *memory);
    assert(memory);
    memory->ds_segment = 0x1200;
    gate_intro_memory_attach(memory);
    gate_far_set_word(0x1000, 0x2000, 0xa51c);
    assert(memory->bytes[0x12000] == 0x1c);
    assert(memory->bytes[0x12001] == 0xa5);
    assert(gate_far_word(0x0fff, 0x2010) == 0xa51c);
    assert(gate_ds_word(0) == 0xa51c);

    gate_ds_set_word(0x54c2, 0x1234);
    assert(gate_far_word(0x1200, 0x54c2) == 0x1234);
    assert(gate_ds_word(0x54c2) == 0x1234);

    /* Offset arithmetic wraps at 16 bits, not at the segment boundary
     * of a host flat pointer. Physical RAM separately wraps at 1 MiB. */
    gate_far_set_word(0x1234, 0xffff, 0x7856);
    assert(gate_far_byte(0x1234, 0xffff) == 0x56);
    assert(gate_far_byte(0x1234, 0) == 0x78);
    gate_far_set_byte(0xffff, 0x0010, 0xcb);
    assert(memory->bytes[0] == 0xcb);

    gate_intro_memory_detach();
    free(memory);
}

static void test_stack_aliases(void)
{
    GateIntroMemory *memory = calloc(1, sizeof *memory);
    uint8_t local[256] = {0};
    uint16_t word = 0;
    GateFarPtr one, two, repeated;
    assert(memory);
    gate_intro_memory_attach(memory);

    one = gate_stack_pointer(local);
    repeated = gate_stack_pointer(local);
    two = gate_stack_pointer(&word);
    assert(one.segment == 0xe000 && one.offset == 0);
    assert(repeated.segment == one.segment && two.segment == 0xe001);
    assert(memory->stack_view_count == 2);
    gate_far_set_byte(one.segment, 0x88, 0x42);
    assert(local[0x88] == 0x42);
    local[0xfe] = 0xa5;
    assert(gate_far_byte(one.segment, 0xfe) == 0xa5);
    gate_far_set_word(two.segment, 0, 0x9876);
    assert(word == 0x9876);

    gate_intro_memory_detach();
    free(memory);
}

int main(void)
{
    test_real_mode_aliasing_and_ds();
    test_stack_aliases();
    puts("native intro DOS memory aliasing, DS and C-stack FAR pointers: OK");
    return 0;
}
