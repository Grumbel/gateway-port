/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Executed by a real DOS MS C 5.1 binary, not by the host Python tests. */
#include "GATEMEM.H"
#include <dos.h>
#include <stdio.h>

static int check(uint16_t actual, uint16_t expected, const char *what)
{
    if (actual != expected) {
        printf("Memory probe: %s: got %04x, expected %04x\n",
               what, actual, expected);
        return 0;
    }
    return 1;
}

int main(void)
{
    unsigned int allocated_segment = 0;
    unsigned char local[4];
    GateFarPtr stack;
    int valid = 1;

    if (_dos_allocmem(0x1000u, &allocated_segment) != 0) {
        puts("Memory probe: DOS allocation of 64K failed");
        return 1;
    }
    gate_dos_bind_ds((uint16_t)allocated_segment);
    valid &= check(gate_ds_segment(), (uint16_t)allocated_segment, "guest DS");
    gate_ds_set_word(0x358eu, 0x4711u);
    valid &= check(gate_ds_word(0x358eu), 0x4711u, "DS read/write");
    valid &= check(gate_far_byte((uint16_t)allocated_segment, 0x358eu), 0x11u,
                   "little-endian low byte");
    valid &= check(gate_far_byte((uint16_t)allocated_segment, 0x358fu), 0x47u,
                   "little-endian high byte");
    gate_far_set_word((uint16_t)allocated_segment, 0xffffu, 0x1234u);
    valid &= check(gate_far_byte((uint16_t)allocated_segment, 0xffffu), 0x34u,
                   "offset wrap low byte");
    valid &= check(gate_far_byte((uint16_t)allocated_segment, 0), 0x12u,
                   "offset wrap high byte");

    local[0] = 0x56u;
    local[1] = 0x78u;
    stack = gate_stack_pointer(local);
    valid &= check(gate_far_byte(stack.segment, stack.offset), 0x56u,
                   "stack SS:offset read");
    gate_far_set_byte(stack.segment, (uint16_t)(stack.offset + 1u), 0xa5u);
    valid &= check(local[1], 0xa5u, "stack SS:offset write");

    if (_dos_freemem(allocated_segment) != 0) {
        puts("Memory probe: DOS free failed");
        return 1;
    }
    puts(valid ? "Memory probe: PASS" : "Memory probe: FAIL");
    return valid ? 0 : 1;
}
