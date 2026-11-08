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
int gate_dos_clear_intro_rtl_prefix(uint16_t lower_segment, uint16_t paragraphs);
int gate_dos_fix_intro_rtl_startup_words(uint16_t lower_segment, uint16_t paragraphs);
int gate_dos_fix_intro_return_pointer(uint16_t lower_segment,
                                      uint16_t paragraphs, GateFarPtr target);
int gate_dos_fix_intro_rtl_offs(uint16_t lower_segment, uint16_t paragraphs);
int gate_dos_fix_intro_rtl_external(uint16_t lower_segment, uint16_t paragraphs,
                                    uint16_t first_segment, uint16_t second_segment);

typedef struct ExpectedOffset {
    uint16_t site, source, expected;
} ExpectedOffset;
static const ExpectedOffset expected_offsets[] = {
    {0x0fb6u, 0x0000u, 0x0f92u}, {0x0fbau, 0x0004u, 0x0f96u},
    {0x0fbeu, 0x0008u, 0x0f9au}, {0x0fc2u, 0x000cu, 0x0f9eu},
    {0x0fc6u, 0x0010u, 0x0fa2u}, {0x0fcau, 0x0014u, 0x0fa6u},
    {0x1538u, 0x001au, 0x14fau}, {0x153cu, 0x0020u, 0x1500u},
    {0x1540u, 0x002au, 0x150au}, {0x1544u, 0x002fu, 0x150fu},
    {0x1548u, 0x0039u, 0x1519u}, {0x154cu, 0x003fu, 0x151fu},
    {0x1550u, 0x0049u, 0x1529u}, {0x1554u, 0x004eu, 0x152eu},
    {0x33dcu, 0x0000u, 0x0c02u}, {0x33e0u, 0x0000u, 0x0c02u},
    {0x33e4u, 0x0000u, 0x0c02u}, {0x33e8u, 0x0000u, 0x0c02u},
    {0x33ecu, 0x0000u, 0x0c02u},
    {0x3244u, 0x0000u, 0x2c44u}, {0x324au, 0x0000u, 0x2c44u},
    {0x33acu, 0x06e4u, 0x3328u}, {0x353eu, 0x0000u, 0x3530u},
    {0x3542u, 0x0004u, 0x3534u}
};
static const ExpectedOffset external_a[] = {
    {0x2814u, 0u, 0x000au}, {0x2852u, 0u, 0x0032u},
    {0x2890u, 0u, 0x0032u}, {0x28ceu, 0u, 0x0053u},
    {0x290cu, 0u, 0x0065u}, {0x294au, 0u, 0x0065u},
    {0x2988u, 0u, 0x0065u}, {0x29c6u, 0u, 0x0065u}
};
static const ExpectedOffset external_b[] = {
    {0x2a93u, 0u, 0x006bu}, {0x2aa7u, 0u, 0x00d6u},
    {0x2abbu, 0u, 0x00d6u}, {0x2acfu, 0u, 0x0133u},
    {0x2ae3u, 0u, 0x0189u}, {0x2af7u, 0u, 0x0189u},
    {0x2b0bu, 0u, 0x0189u}, {0x2b1fu, 0u, 0x0189u},
    {0x2b33u, 0u, 0x01e3u}, {0x2b47u, 0u, 0x0272u},
    {0x2b5bu, 0u, 0x0440u}
};

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
    GateFarPtr callback;
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
    if (gate_dos_clear_intro_rtl_prefix(0x2000u, 0x1009u) != -1 ||
        gate_far_byte(data_segment, 2u) != (uint8_t)(pattern(0x2bu) ^ 0x5au) ||
        gate_dos_clear_intro_rtl_prefix(0x2000u, 0x100au) != 0)
        abort();
    for (i = 2u; i < 66u; ++i)
        if (gate_far_byte(data_segment, (uint16_t)i) != 0u)
            abort();
    if (gate_far_byte(data_segment, 1u) != (uint8_t)(pattern(0x2au) ^ 0x5au) ||
        gate_far_byte(data_segment, 66u) != (uint8_t)(pattern(0x6bu) ^ 0x5au))
        abort();
    callback.segment = data_segment;
    callback.offset = 0x6001u;
    gate_far_set_byte(data_segment, 0x6000u, 0xcfu);
    gate_far_set_byte(data_segment, 0x6001u, 0xcbu);
    gate_far_set_byte(data_segment, 0x6002u, 0x8bu);
    gate_far_set_byte(data_segment, 0x601eu, 0xfau);
    gate_far_set_byte(data_segment, 0x601fu, 0x8bu);
    gate_far_set_byte(data_segment, 0x6020u, 0x46u);
    gate_far_set_byte(data_segment, 0x6021u, 0x06u);
    gate_far_set_byte(data_segment, 0x6022u, 0xa3u);
    gate_ds_set_word(0x049du, 0u);
    gate_ds_set_word(0x049fu, 0u);
    if (gate_dos_fix_intro_return_pointer(0x2000u, 0x1009u, callback) != -1)
        abort();
    gate_far_set_byte(data_segment, 0x6002u, 0x90u);
    if (gate_dos_fix_intro_return_pointer(0x2000u, 0x100au, callback) != -2 ||
        gate_ds_word(0x049du) != 0u)
        abort();
    gate_far_set_byte(data_segment, 0x6002u, 0x8bu);
    if (gate_dos_fix_intro_return_pointer(0x2000u, 0x100au, callback) != 0 ||
        gate_ds_word(0x049du) != callback.offset ||
        gate_ds_word(0x049fu) != callback.segment)
        abort();
    gate_ds_set_word(0x0525u, 0u);
    gate_ds_set_word(0x26d6u, 0u);
    gate_ds_set_word(0x2b74u, 0u);
    gate_ds_set_word(0x2bc6u, 0u);
    gate_ds_set_word(0x2c0eu, 0u);
    gate_ds_set_word(0x2c24u, 1u);
    if (gate_dos_fix_intro_rtl_startup_words(0x2000u, 0x100au) != -2 ||
        gate_ds_word(0x0525u) != 0u)
        abort();
    gate_ds_set_word(0x2c24u, 0u);
    if (gate_dos_fix_intro_rtl_startup_words(0x2000u, 0x1009u) != -1 ||
        gate_dos_fix_intro_rtl_startup_words(0x2000u, 0x100au) != 0 ||
        gate_ds_word(0x0525u) != 0x0527u ||
        gate_ds_word(0x26d6u) != 0x16d4u ||
        gate_ds_word(0x2b74u) != 0x0b57u ||
        gate_ds_word(0x2bc6u) != 0x2b76u ||
        gate_ds_word(0x2c0eu) != 0x2c12u ||
        gate_ds_word(0x2c24u) != 0x65f0u)
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
    for (i = 0u; i < sizeof external_a / sizeof external_a[0]; ++i) {
        gate_ds_set_word(external_a[i].site, 0u);
        gate_ds_set_word((uint16_t)(external_a[i].site + 2u), 0u);
    }
    for (i = 0u; i < sizeof external_b / sizeof external_b[0]; ++i) {
        gate_ds_set_word(external_b[i].site, 0u);
        gate_ds_set_word((uint16_t)(external_b[i].site + 2u), 0u);
    }
    gate_ds_set_word((uint16_t)(external_b[10].site + 2u), 0x9876u);
    if (gate_dos_fix_intro_rtl_external(0x2000u, 0x100au, 0x3333u, 0x4444u) != -2 ||
        gate_ds_word(external_a[0].site) != 0u)
        abort();
    gate_ds_set_word((uint16_t)(external_b[10].site + 2u), 0u);
    if (gate_dos_fix_intro_rtl_external(0x2000u, 0x1009u, 0x3333u, 0x4444u) != -1 ||
        gate_dos_fix_intro_rtl_external(0x2000u, 0x100au, 0u, 0x4444u) != -1 ||
        gate_dos_fix_intro_rtl_external(0x2000u, 0x100au, 0x3333u, 0x4444u) != 0)
        abort();
    for (i = 0u; i < sizeof external_a / sizeof external_a[0]; ++i)
        if (gate_ds_word(external_a[i].site) != external_a[i].expected ||
            gate_ds_word((uint16_t)(external_a[i].site + 2u)) != 0x3333u)
            abort();
    for (i = 0u; i < sizeof external_b / sizeof external_b[0]; ++i)
        if (gate_ds_word(external_b[i].site) != external_b[i].expected ||
            gate_ds_word((uint16_t)(external_b[i].site + 2u)) != 0x4444u)
            abort();
    for (i = 0u; i < sizeof expected_offsets / sizeof expected_offsets[0]; ++i)
        gate_ds_set_word(expected_offsets[i].site, expected_offsets[i].source);
    gate_ds_set_word(expected_offsets[23].site, 0x9876u);
    if (gate_dos_fix_intro_rtl_offs(0x2000u, 0x100au) != -2 ||
        gate_ds_word(expected_offsets[0].site) != expected_offsets[0].source)
        abort();
    gate_ds_set_word(expected_offsets[23].site, expected_offsets[23].source);
    if (gate_dos_fix_intro_rtl_offs(0x2000u, 0x1009u) != -1 ||
        gate_dos_fix_intro_rtl_offs(0x2000u, 0x100au) != 0)
        abort();
    for (i = 0u; i < sizeof expected_offsets / sizeof expected_offsets[0]; ++i)
        if (gate_ds_word(expected_offsets[i].site) != expected_offsets[i].expected)
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
    puts("static EXE/RTL copy, 27 seg + 24 off + 19 external pointers host: PASS");
    return 0;
}
