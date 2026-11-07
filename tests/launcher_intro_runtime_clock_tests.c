/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gate_exe.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

uint32_t gate_rtl_8fa2(void);
uint32_t gate_rtl_933a(uint32_t lhs, uint32_t rhs);
uint32_t gate_rtl_929e(int32_t dividend, int32_t divisor);

static uint8_t stack_bytes[65536];
static uint16_t ds[32768];
static uint16_t cursor_reset;
static uint32_t seconds;
static uint16_t fraction;
static unsigned calls;

uint16_t gate_ds_word(uint16_t offset) { return ds[offset / 2]; }

GateFarPtr gate_stack_pointer(void *unused)
{
    (void)unused;
    return (GateFarPtr){0x9000, 0xfffe};
}

uint16_t gate_far_word(uint16_t segment, uint16_t offset)
{
    if (segment == 0x5555 && offset == 0x3582)
        return cursor_reset;
    assert(segment == 0x9000);
    return (uint16_t)(stack_bytes[offset] |
                      ((uint16_t)stack_bytes[(uint16_t)(offset + 1)] << 8));
}

void gate_far_set_word(uint16_t segment, uint16_t offset, uint16_t value)
{
    if (segment == 0x5555 && offset == 0x3582) {
        cursor_reset = value;
        return;
    }
    assert(segment == 0x9000);
    stack_bytes[offset] = (uint8_t)value;
    stack_bytes[(uint16_t)(offset + 1)] = (uint8_t)(value >> 8);
}

void gate_far_12a5_34d8(GateFarPtr destination)
{
    assert(cursor_reset == 0); /* reset precedes original sampler call */
    ++calls;
    gate_far_set_word(destination.segment, destination.offset, (uint16_t)seconds);
    gate_far_set_word(destination.segment, (uint16_t)(destination.offset + 2),
                      (uint16_t)(seconds >> 16));
    gate_far_set_word(destination.segment, (uint16_t)(destination.offset + 4), fraction);
}

int main(void)
{
    memset(stack_bytes, 0xcc, sizeof stack_bytes);
    ds[0x4e24 / 2] = 0x5555;
    ds[0x3584 / 2] = 0x2340;
    ds[0x3586 / 2] = 0x0001;
    ds[0x3588 / 2] = 1200;
    seconds = 0x00012345;
    fraction = 900;
    cursor_reset = 0xbeef;
    assert(gate_rtl_8fa2() == 4700);
    assert(calls == 1);

    ds[0x3584 / 2] = 0xfffe;
    ds[0x3586 / 2] = 0xffff;
    ds[0x3588 / 2] = 99;
    seconds = 2;
    fraction = 0xffff;
    cursor_reset = 0xbeef;
    assert(gate_rtl_8fa2() == 3900);
    assert(calls == 2);

    assert(gate_rtl_933a(0xffffffffu, 1000u) == 0xfffffc18u);
    assert(gate_rtl_933a(0x12345678u, 0x87654321u) ==
           (uint32_t)(0x12345678u * 0x87654321u));
    assert((int32_t)gate_rtl_929e(3600, 60) == 60);
    assert((int32_t)gate_rtl_929e(-3600, 60) == -60);
    assert((int32_t)gate_rtl_929e(3600, -60) == -60);
    assert((int32_t)gate_rtl_929e(-3600, -60) == 60);
    assert((int32_t)gate_rtl_929e(-100, 60) == -1);
    assert(gate_rtl_929e((int32_t)0x80000000u, -1) == 0x80000000u);
    puts("RTL:8fa2 clock and RTL:933a multiply: OK");
    return 0;
}
