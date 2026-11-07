/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifdef NDEBUG
#undef NDEBUG
#endif

#include "gate_intro_host_memory.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    GateIntroMemory *memory = calloc(1, sizeof *memory);
    uint8_t *maps = calloc(2u * GATE_INTRO_RAM_BYTES, 1);
    uint8_t local[2] = {17, 18};
    GateFarPtr stack;
    assert(memory && maps);
    memory->input_reads = maps;
    memory->written_bytes = maps + GATE_INTRO_RAM_BYTES;
    memory->ds_segment = 0x1000;
    gate_intro_memory_attach(memory);

    /* First read of an original RAM byte is an input dependency. */
    assert(gate_far_byte(0x1000, 0x1234) == 0);
    assert(memory->input_reads[0x11234] == 1);
    /* A write first makes the initial RAM value irrelevant. */
    gate_far_set_byte(0x1000, 0x1235, 0x55);
    assert(gate_far_byte(0x1000, 0x1235) == 0x55);
    assert(memory->input_reads[0x11235] == 0);
    assert(memory->written_bytes[0x11235] == 1);

    /* Segment aliases refer to the same physical byte. */
    gate_far_set_byte(0x1010, 0x0234, 99);
    assert(gate_far_byte(0x1000, 0x0334) == 99);
    assert(memory->input_reads[0x10334] == 0);

    /* A 16-bit word read wraps OFFSET independently of its segment. */
    assert(gate_far_word(0x2000, 0xffff) == 0);
    assert(memory->input_reads[0x2ffff] == 1);
    assert(memory->input_reads[0x20000] == 1);
    assert(memory->input_reads[0x30000] == 0);

    /* Memory wraps at 20 bits for a high-segment FAR pointer. */
    gate_far_set_byte(0xffff, 0xffff, 7);
    assert(gate_far_byte(0x0ffe, 0x000f) == 7);
    assert(memory->input_reads[0x0ffef] == 0);

    /* Synthetic FAR segments point into native stack storage and must
     * never be treated as dependencies on the captured DOS image. */
    stack = gate_stack_pointer(local);
    assert(gate_far_byte(stack.segment, 0) == 17);
    gate_far_set_byte(stack.segment, 1, 20);
    assert(local[1] == 20);
    assert(memory->input_reads[0xe0000] == 0);

    gate_intro_memory_detach();
    free(maps);
    free(memory);
    puts("original source-C initial RAM read dependency tracking: OK");
    return 0;
}
