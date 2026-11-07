/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gate_exe.h"
#include "gate_intro_host_memory.h"
#include "gate_rtl.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    GateIntroMemory *ram = calloc(1, sizeof *ram);
    char stack_text[] = "GATE_300.PIC";
    GateFarPtr local;
    uint32_t i;
    assert(ram);
    ram->ds_segment = 0x2a7a;
    gate_intro_memory_attach(ram);
    local = gate_stack_pointer(stack_text);
    assert(gate_far_02a5_2bba(local) == 12);
    assert(gate_rtl_8298(local) == 12);
    assert(gate_far_02a5_2bba((GateFarPtr){0x0100, 0x0200}) == 0);
    gate_far_set_byte(0x0100, 0xffff, 'X');
    gate_far_set_byte(0x0100, 0, 'Y');
    gate_far_set_byte(0x0100, 1, 0);
    assert(gate_far_02a5_2bba((GateFarPtr){0x0100, 0xffff}) == 2);
    assert(gate_rtl_8298((GateFarPtr){0x0100, 0xffff}) == 2);

    for (i = 0; i < 65535; ++i)
        gate_far_set_byte(0x0100, (uint16_t)i, 'Z');
    assert(gate_far_02a5_2bba((GateFarPtr){0x0100, 0}) == 65534);
    assert(gate_rtl_8298((GateFarPtr){0x0100, 0}) == 65534);

    gate_intro_memory_detach();
    free(ram);
    puts("linked original 12a5:2bba FAR strlen, wrap and scan bound: OK");
    return 0;
}
