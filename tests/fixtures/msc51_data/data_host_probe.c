/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Synthetic DOS file/memory model tests only the copy contract. */
#include "GATEDOS.H"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int gate_dos_bind_intro_aliases(uint16_t lower_segment, uint16_t paragraphs);
int gate_dos_load_intro_static_data(uint16_t handle, uint16_t lower_segment,
                                    uint16_t paragraphs);

static uint8_t memory[0x100000];
static uint16_t data_segment;
static unsigned int calls_seek, calls_read, seek_bad, read_short, read_bad;

void gate_dos_bind_ds(uint16_t segment) { data_segment = segment; }
uint16_t gate_ds_segment(void) { return data_segment; }
uint8_t gate_far_byte(uint16_t seg, uint16_t off)
{
    return memory[(unsigned long)seg * 16ul + off];
}
void gate_far_set_byte(uint16_t seg, uint16_t off, uint8_t value)
{
    memory[(unsigned long)seg * 16ul + off] = value;
}
uint16_t gate_far_word(uint16_t seg, uint16_t off)
{
    return (uint16_t)(gate_far_byte(seg, off) |
        (uint16_t)(gate_far_byte(seg, (uint16_t)(off + 1u)) << 8));
}
void gate_far_set_word(uint16_t seg, uint16_t off, uint16_t value)
{
    gate_far_set_byte(seg, off, (uint8_t)value);
    gate_far_set_byte(seg, (uint16_t)(off + 1u), (uint8_t)(value >> 8));
}
uint16_t gate_ds_word(uint16_t off) { return gate_far_word(data_segment, off); }
void gate_ds_set_word(uint16_t off, uint16_t value)
{
    gate_far_set_word(data_segment, off, value);
}

static uint8_t pattern(unsigned int index)
{
    return (uint8_t)(((index & 0xffu) ^ ((index >> 8) * 17u)) & 0xffu);
}

GateDosFileResult gate_dos_file_seek(uint16_t handle, uint16_t origin,
                                     uint32_t offset)
{
    GateDosFileResult result;
    ++calls_seek;
    if (handle != 7u || origin != 0u || offset != 0x41e6ul)
        abort();
    result.carry = 0u;
    result.ax = seek_bad ? 0u : 0x41e6u;
    result.dx = 0u;
    return result;
}

GateDosFileResult gate_dos_file_read(uint16_t handle, GateFarPtr target,
                                     uint16_t length)
{
    GateDosFileResult result;
    unsigned int i;
    ++calls_read;
    if (handle != 7u || target.segment != data_segment ||
        target.offset != 0x3592u || length != 0x1758u)
        abort();
    result.carry = (uint8_t)(read_bad != 0u);
    result.dx = 0u;
    result.ax = read_short ? length - 1u : length;
    for (i = 0u; i < result.ax && !read_bad; ++i)
        gate_far_set_byte(target.segment, (uint16_t)(target.offset + i),
                         pattern(0x41e6u + i));
    return result;
}

int main(void)
{
    unsigned int i;
    memset(memory, 0xc3, sizeof memory);
    if (gate_dos_bind_intro_aliases(0x2000u, 0x100au) != 0)
        abort();
    gate_ds_set_word(0x3590u, 0x1133u);
    gate_ds_set_word(0x4ceau, 0x7799u);
    if (gate_dos_load_intro_static_data(7u, 0x2000u, 0x1009u) != -1 ||
        gate_dos_load_intro_static_data(7u, 0u, 0x100au) != -1 ||
        calls_seek != 0u || calls_read != 0u)
        abort();
    if (gate_dos_load_intro_static_data(7u, 0x2000u, 0x100au) != 0 ||
        calls_seek != 1u || calls_read != 1u)
        abort();
    for (i = 0u; i < 0x1758u; ++i)
        if (gate_far_byte(data_segment, (uint16_t)(0x3592u + i)) !=
            pattern(0x41e6u + i))
            abort();
    if (gate_ds_word(0x3590u) != 0x1133u ||
        gate_ds_word(0x4ceau) != 0x7799u ||
        gate_ds_word(0x4d82u) != 0x2000u ||
        gate_ds_word(0x4d84u) != 0x200au)
        abort();
    seek_bad = 1u;
    if (gate_dos_load_intro_static_data(7u, 0x2000u, 0x100au) != -2 ||
        calls_read != 1u)
        abort();
    seek_bad = 0u;
    read_short = 1u;
    if (gate_dos_load_intro_static_data(7u, 0x2000u, 0x100au) != -3)
        abort();
    read_short = 0u;
    read_bad = 1u;
    if (gate_dos_load_intro_static_data(7u, 0x2000u, 0x100au) != -3)
        abort();
    puts("static EXE copy host: PASS (boundaries/seek/short read/failure)");
    return 0;
}
