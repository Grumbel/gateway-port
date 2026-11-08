/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Include the staged DOS implementation in this test translation unit so
 * the original-derived static relocation manifest can be exercised without
 * duplicating 98 callsite/target pairs in a second source file. */
#include "DATA.C"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t memory[0x100000];
static uint16_t data_segment;

uint16_t gate_ds_segment(void) { return data_segment; }
void gate_dos_bind_ds(uint16_t segment) { data_segment = segment; }
uint8_t gate_far_byte(uint16_t segment, uint16_t offset)
{
    return memory[(unsigned long)segment * 16ul + offset];
}
void gate_far_set_byte(uint16_t segment, uint16_t offset, uint8_t value)
{
    memory[(unsigned long)segment * 16ul + offset] = value;
}
uint16_t gate_far_word(uint16_t segment, uint16_t offset)
{
    return (uint16_t)(gate_far_byte(segment, offset) |
        ((uint16_t)gate_far_byte(segment, (uint16_t)(offset + 1u)) << 8));
}
void gate_far_set_word(uint16_t segment, uint16_t offset, uint16_t value)
{
    gate_far_set_byte(segment, offset, (uint8_t)value);
    gate_far_set_byte(segment, (uint16_t)(offset + 1u), (uint8_t)(value >> 8));
}
uint16_t gate_ds_word(uint16_t offset) { return gate_far_word(data_segment, offset); }
void gate_ds_set_word(uint16_t offset, uint16_t value)
{
    gate_far_set_word(data_segment, offset, value);
}
int gate_dos_bind_intro_aliases(uint16_t lower_segment, uint16_t paragraphs)
{
    (void)lower_segment;
    (void)paragraphs;
    return -1;
}
GateDosFileResult gate_dos_file_seek(uint16_t handle, uint16_t origin, uint32_t offset)
{
    GateDosFileResult result;
    (void)handle; (void)origin; (void)offset;
    memset(&result, 0, sizeof result);
    result.carry = 1u;
    return result;
}
GateDosFileResult gate_dos_file_read(uint16_t handle, GateFarPtr target, uint16_t length)
{
    GateDosFileResult result;
    (void)handle; (void)target; (void)length;
    memset(&result, 0, sizeof result);
    result.carry = 1u;
    return result;
}

int main(void)
{
    unsigned int i;
    uint16_t segment = 0x3000u;
    uint16_t first_site = gate_resident_self_calls[0].site;
    uint16_t last_site = gate_resident_self_calls[
        sizeof gate_resident_self_calls / sizeof gate_resident_self_calls[0] - 1u].site;

    if (sizeof gate_resident_self_calls / sizeof gate_resident_self_calls[0] != 98u)
        abort();
    memset(memory, 0x90, sizeof memory);
    for (i = 0u; i < sizeof gate_resident_self_calls /
                      sizeof gate_resident_self_calls[0]; ++i) {
        uint16_t site = gate_resident_self_calls[i].site;
        if (site + 5u > 0x3d00u || (i && site <= gate_resident_self_calls[i - 1u].site + 4u))
            abort();
        gate_far_set_byte(segment, site, 0x9au);
        gate_far_set_word(segment, (uint16_t)(site + 1u), 0u);
        gate_far_set_word(segment, (uint16_t)(site + 3u), 0u);
    }
    if (gate_dos_relocate_intro_resident_self_calls(segment, 0x03cfu) != -1)
        abort();
    gate_far_set_byte(segment, last_site, 0x90u);
    if (gate_dos_relocate_intro_resident_self_calls(segment, 0x03d0u) != -2 ||
        gate_far_word(segment, (uint16_t)(first_site + 1u)) != 0u)
        abort();
    gate_far_set_byte(segment, last_site, 0x9au);
    if (gate_dos_relocate_intro_resident_self_calls(segment, 0x03d0u) != 0)
        abort();
    for (i = 0u; i < sizeof gate_resident_self_calls /
                      sizeof gate_resident_self_calls[0]; ++i) {
        uint16_t site = gate_resident_self_calls[i].site;
        if (gate_far_byte(segment, site) != 0x9au ||
            gate_far_word(segment, (uint16_t)(site + 1u)) !=
                gate_resident_self_calls[i].target ||
            gate_far_word(segment, (uint16_t)(site + 3u)) != segment)
            abort();
    }
    if (gate_far_byte(segment, (uint16_t)(first_site - 1u)) != 0x90u ||
        gate_far_byte(segment, (uint16_t)(last_site + 5u)) != 0x90u)
        abort();
    puts("98 resident self CALL FAR relocations: PASS");
    return 0;
}
