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
int gate_dos_load_intro_rtl_data(uint16_t handle, uint16_t lower_segment,
                                 uint16_t paragraphs);
int gate_dos_fix_intro_rtl_segs(uint16_t lower_segment, uint16_t paragraphs,
                                uint16_t resident_segment);

static const uint16_t observed_ds_sites[] = {
    0x0fb8u, 0x0fbcu, 0x0fc0u, 0x0fc4u, 0x0fc8u, 0x0fccu,
    0x153au, 0x153eu, 0x1542u, 0x1546u, 0x154au, 0x154eu,
    0x1552u, 0x1556u, 0x26d8u, 0x2b78u, 0x2c10u, 0x3246u,
    0x324cu, 0x33aeu, 0x3540u, 0x3544u
};
static const uint16_t observed_cs_sites[] = {
    0x33deu, 0x33e2u, 0x33e6u, 0x33eau, 0x33eeu
};

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
    if (origin != 0u ||
        !((handle == 7u && offset == 0x41e6ul) ||
          (handle == 8u && offset == 0x29ul)))
        abort();
    result.carry = 0u;
    result.ax = seek_bad ? 0u : (uint16_t)offset;
    result.dx = 0u;
    return result;
}

GateDosFileResult gate_dos_file_read(uint16_t handle, GateFarPtr target,
                                     uint16_t length)
{
    GateDosFileResult result;
    unsigned int i;
    ++calls_read;
    if (target.segment != data_segment ||
        !((handle == 7u && target.offset == 0x3592u && length == 0x1758u) ||
          (handle == 8u && target.offset == 0u && length == 0x3592u)))
        abort();
    result.carry = (uint8_t)(read_bad != 0u);
    result.dx = 0u;
    result.ax = read_short ? length - 1u : length;
    for (i = 0u; i < result.ax && !read_bad; ++i)
        gate_far_set_byte(target.segment, (uint16_t)(target.offset + i),
                         (uint8_t)(pattern((handle == 7u ? 0x41e6u : 0x29u) + i) ^
                                   (handle == 8u ? 0x5au : 0u)));
    return result;
}

int main(void)
{
    unsigned int i;
    uint8_t boundary;
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
    boundary = gate_far_byte(data_segment, 0x3592u);
    if (gate_dos_load_intro_rtl_data(8u, 0x2000u, 0x1009u) != -1 ||
        gate_dos_load_intro_rtl_data(8u, 0x2000u, 0x100au) != 0 ||
        calls_seek != 2u || calls_read != 2u)
        abort();
    for (i = 0u; i < 0x3592u; ++i)
        if (gate_far_byte(data_segment, (uint16_t)i) !=
            (uint8_t)(pattern(0x29u + i) ^ 0x5au))
            abort();
    if (gate_far_byte(data_segment, 0x3592u) != boundary ||
        gate_ds_word(0x4d82u) != 0x2000u ||
        gate_ds_word(0x4d84u) != 0x200au)
        abort();
    /* Synthetic file bytes are not zero at every observed source-word
     * site. Turn only those exact words into a zero-source relocation
     * fixture after verifying the entire raw source copy above. */
    for (i = 0u; i < sizeof observed_ds_sites / sizeof observed_ds_sites[0]; ++i)
        gate_ds_set_word(observed_ds_sites[i], 0u);
    for (i = 0u; i < sizeof observed_cs_sites / sizeof observed_cs_sites[0]; ++i)
        gate_ds_set_word(observed_cs_sites[i], 0u);
    gate_ds_set_word(observed_cs_sites[4], 0x1234u);
    if (gate_dos_fix_intro_rtl_segs(0x2000u, 0x100au, 0x1234u) != -2 ||
        gate_ds_word(observed_ds_sites[0]) != 0u ||
        gate_ds_word(observed_cs_sites[0]) != 0u ||
        gate_ds_word(observed_cs_sites[4]) != 0x1234u)
        abort();
    gate_ds_set_word(observed_cs_sites[4], 0u);
    if (gate_dos_fix_intro_rtl_segs(0x2000u, 0x1009u, 0x1234u) != -1 ||
        gate_dos_fix_intro_rtl_segs(0x2000u, 0x100au, 0u) != -1 ||
        gate_dos_fix_intro_rtl_segs(0x2000u, 0x100au, 0x1234u) != 0)
        abort();
    for (i = 0u; i < sizeof observed_ds_sites / sizeof observed_ds_sites[0]; ++i)
        if (gate_ds_word(observed_ds_sites[i]) != 0x200au)
            abort();
    for (i = 0u; i < sizeof observed_cs_sites / sizeof observed_cs_sites[0]; ++i)
        if (gate_ds_word(observed_cs_sites[i]) != 0x1234u)
            abort();
    if (gate_far_byte(data_segment, 0x3592u) != boundary ||
        gate_ds_word(0x4d82u) != 0x2000u ||
        gate_ds_word(0x4d84u) != 0x200au)
        abort();
    seek_bad = 1u;
    if (gate_dos_load_intro_static_data(7u, 0x2000u, 0x100au) != -2 ||
        gate_dos_load_intro_rtl_data(8u, 0x2000u, 0x100au) != -2 ||
        calls_read != 2u)
        abort();
    seek_bad = 0u;
    read_short = 1u;
    if (gate_dos_load_intro_static_data(7u, 0x2000u, 0x100au) != -3)
        abort();
    if (gate_dos_load_intro_rtl_data(8u, 0x2000u, 0x100au) != -3)
        abort();
    read_short = 0u;
    read_bad = 1u;
    if (gate_dos_load_intro_static_data(7u, 0x2000u, 0x100au) != -3)
        abort();
    if (gate_dos_load_intro_rtl_data(8u, 0x2000u, 0x100au) != -3)
        abort();
    puts("static EXE/RTL copy and 27 segment fixes host: PASS");
    return 0;
}
