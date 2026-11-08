/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Deliberately generated test bytes, not redistributed game data.
 * The only main() is this isolated MS C 5.1/DOSBox probe. */
#include "GATEDOS.H"
#include <dos.h>
#include <stdio.h>

int gate_dos_bind_intro_aliases(uint16_t lower_segment, uint16_t paragraphs);
int gate_dos_load_intro_static_data(uint16_t handle, uint16_t lower_segment,
                                    uint16_t paragraphs);
int gate_dos_load_intro_rtl_data(uint16_t handle, uint16_t lower_segment,
                                 uint16_t paragraphs);
int gate_dos_clear_intro_rtl_prefix(uint16_t lower_segment, uint16_t paragraphs);
int gate_dos_fix_intro_rtl_segs(uint16_t lower_segment, uint16_t paragraphs,
                                uint16_t resident_segment);
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

/* MS C 5.1 rejects automatic array initialization in a function (C2073).
 * Keep this filename in static storage; gate_stack_pointer() obtains its
 * actual segment and offset, regardless of the storage segment. */
static char source_name[] = "DATASRC.BIN";
static char rtl_source_name[] = "DATARTL.BIN";

static uint8_t pattern(unsigned int index)
{
    return (uint8_t)(((index & 0xffu) ^ ((index >> 8) * 17u)) & 0xffu);
}

/* DOS/Microsoft C 5.1 probe files contain no game data. The RTL fixture
 * includes the 0x29-byte prefix so its seek/copy boundaries are tested. */
static int write_fixture(char *name, unsigned int size, uint8_t mask)
{
    uint8_t block[128];
    unsigned int i;
    FILE *file;
    int good = 1;

    file = fopen(name, "wb");
    if (file == 0)
        return 0;
    for (i = 0u; i < size; i += (unsigned int)sizeof block) {
        unsigned int j;
        unsigned int length = size - i;
        if (length > sizeof block)
            length = sizeof block;
        for (j = 0u; j < length; ++j)
            block[j] = (uint8_t)(pattern(i + j) ^ mask);
        if (fwrite(block, 1u, length, file) != length) {
            good = 0;
            break;
        }
    }
    if (fclose(file) != 0)
        good = 0;
    return good;
}

int main(void)
{
    unsigned int allocated = 0u;
    unsigned int i;
    GateDosFileResult opened;
    GateDosFileResult closed;
    int good;

    good = write_fixture(source_name, 0x593eu, 0u);
    good &= write_fixture(rtl_source_name, 0x35bbu, 0x5au);
    if (!good || _dos_allocmem(0x100au, &allocated) != 0) {
        remove(source_name);
        remove(rtl_source_name);
        puts("Static EXE/RTL data probe: FAIL fixture or memory allocation");
        return 1;
    }
    good &= gate_dos_bind_intro_aliases((uint16_t)allocated, 0x100au) == 0;
    gate_ds_set_word(0x3590u, 0x1133u);
    gate_ds_set_word(0x4ceau, 0x7799u);

    opened = gate_dos_file_open(gate_stack_pointer(source_name), 0u, 0u);
    if (opened.carry)
        good = 0;
    else {
        good &= gate_dos_load_intro_static_data(opened.ax,
                     (uint16_t)allocated, 0x1009u) == -1;
        good &= gate_dos_load_intro_static_data(opened.ax,
                     (uint16_t)allocated, 0x100au) == 0;
        for (i = 0u; i < 0x1758u; ++i) {
            if (gate_ds_segment() != (uint16_t)(allocated + 10u) ||
                gate_far_byte(gate_ds_segment(), (uint16_t)(0x3592u + i)) !=
                pattern((unsigned int)(0x41e6u + i))) {
                good = 0;
                break;
            }
        }
        closed = gate_dos_file_close(opened.ax);
        good &= closed.carry == 0u;
    }
    good &= gate_ds_word(0x3590u) == 0x1133u;
    good &= gate_ds_word(0x4ceau) == 0x7799u;
    good &= gate_ds_word(0x4d82u) == (uint16_t)allocated;
    good &= gate_ds_word(0x4d84u) == (uint16_t)(allocated + 10u);

    opened = gate_dos_file_open(gate_stack_pointer(rtl_source_name), 0u, 0u);
    if (opened.carry)
        good = 0;
    else {
        good &= gate_dos_load_intro_rtl_data(opened.ax,
                     (uint16_t)allocated, 0x1009u) == -1;
        good &= gate_dos_load_intro_rtl_data(opened.ax,
                     (uint16_t)allocated, 0x100au) == 0;
        for (i = 0u; i < 0x3592u; ++i) {
            if (gate_ds_segment() != (uint16_t)(allocated + 10u) ||
                gate_far_byte(gate_ds_segment(), (uint16_t)i) !=
                (uint8_t)(pattern((unsigned int)(0x29u + i)) ^ 0x5au)) {
                good = 0;
                break;
            }
        }
        closed = gate_dos_file_close(opened.ax);
        good &= closed.carry == 0u;
    }
    /* The RTL source occupies DS:0000..3591. It must leave the EXE data
     * and the two overlapping segment selectors untouched. */
    good &= gate_ds_word(0x4ceau) == 0x7799u;
    good &= gate_ds_word(0x4d82u) == (uint16_t)allocated;
    good &= gate_ds_word(0x4d84u) == (uint16_t)(allocated + 10u);
    good &= gate_far_byte(gate_ds_segment(), 0x3592u) == pattern(0x41e6u);
    good &= gate_dos_clear_intro_rtl_prefix((uint16_t)allocated, 0x1009u) == -1;
    good &= gate_far_byte(gate_ds_segment(), 2u) ==
            (uint8_t)(pattern(0x2bu) ^ 0x5au);
    good &= gate_dos_clear_intro_rtl_prefix((uint16_t)allocated, 0x100au) == 0;
    for (i = 2u; i < 66u; ++i)
        good &= gate_far_byte(gate_ds_segment(), (uint16_t)i) == 0u;
    good &= gate_far_byte(gate_ds_segment(), 1u) ==
            (uint8_t)(pattern(0x2au) ^ 0x5au);
    good &= gate_far_byte(gate_ds_segment(), 66u) ==
            (uint8_t)(pattern(0x6bu) ^ 0x5au);
    /* Stage a separate generated source-zero image of the 27 observed
     * 100ms segment-word sites. The full raw-source comparison above
     * has already verified the bounded seek/read behavior. */
    for (i = 0u; i < sizeof observed_ds_sites / sizeof observed_ds_sites[0]; ++i)
        gate_ds_set_word(observed_ds_sites[i], 0u);
    for (i = 0u; i < sizeof observed_cs_sites / sizeof observed_cs_sites[0]; ++i)
        gate_ds_set_word(observed_cs_sites[i], 0u);
    gate_ds_set_word(observed_cs_sites[4], 0x1234u);
    good &= gate_dos_fix_intro_rtl_segs((uint16_t)allocated, 0x100au, 0x1234u) == -2;
    good &= gate_ds_word(observed_ds_sites[0]) == 0u;
    gate_ds_set_word(observed_cs_sites[4], 0u);
    good &= gate_dos_fix_intro_rtl_segs((uint16_t)allocated, 0x1009u, 0x1234u) == -1;
    good &= gate_dos_fix_intro_rtl_segs((uint16_t)allocated, 0x100au, 0u) == -1;
    good &= gate_dos_fix_intro_rtl_segs((uint16_t)allocated, 0x100au, 0x1234u) == 0;
    for (i = 0u; i < sizeof observed_ds_sites / sizeof observed_ds_sites[0]; ++i)
        good &= gate_ds_word(observed_ds_sites[i]) == (uint16_t)(allocated + 10u);
    for (i = 0u; i < sizeof observed_cs_sites / sizeof observed_cs_sites[0]; ++i)
        good &= gate_ds_word(observed_cs_sites[i]) == 0x1234u;
    for (i = 0u; i < sizeof external_a / sizeof external_a[0]; ++i) {
        gate_ds_set_word(external_a[i].site, 0u);
        gate_ds_set_word((uint16_t)(external_a[i].site + 2u), 0u);
    }
    for (i = 0u; i < sizeof external_b / sizeof external_b[0]; ++i) {
        gate_ds_set_word(external_b[i].site, 0u);
        gate_ds_set_word((uint16_t)(external_b[i].site + 2u), 0u);
    }
    gate_ds_set_word((uint16_t)(external_b[10].site + 2u), 0x1234u);
    good &= gate_dos_fix_intro_rtl_external((uint16_t)allocated, 0x100au,
                                            0x3333u, 0x4444u) == -2;
    good &= gate_ds_word(external_a[0].site) == 0u;
    gate_ds_set_word((uint16_t)(external_b[10].site + 2u), 0u);
    good &= gate_dos_fix_intro_rtl_external((uint16_t)allocated, 0x1009u,
                                            0x3333u, 0x4444u) == -1;
    good &= gate_dos_fix_intro_rtl_external((uint16_t)allocated, 0x100au,
                                            0u, 0x4444u) == -1;
    good &= gate_dos_fix_intro_rtl_external((uint16_t)allocated, 0x100au,
                                            0x3333u, 0x4444u) == 0;
    for (i = 0u; i < sizeof external_a / sizeof external_a[0]; ++i) {
        good &= gate_ds_word(external_a[i].site) == external_a[i].expected;
        good &= gate_ds_word((uint16_t)(external_a[i].site + 2u)) == 0x3333u;
    }
    for (i = 0u; i < sizeof external_b / sizeof external_b[0]; ++i) {
        good &= gate_ds_word(external_b[i].site) == external_b[i].expected;
        good &= gate_ds_word((uint16_t)(external_b[i].site + 2u)) == 0x4444u;
    }
    for (i = 0u; i < sizeof expected_offsets / sizeof expected_offsets[0]; ++i)
        gate_ds_set_word(expected_offsets[i].site, expected_offsets[i].source);
    gate_ds_set_word(expected_offsets[23].site, 0x9876u);
    good &= gate_dos_fix_intro_rtl_offs((uint16_t)allocated, 0x100au) == -2;
    good &= gate_ds_word(expected_offsets[0].site) == expected_offsets[0].source;
    gate_ds_set_word(expected_offsets[23].site, expected_offsets[23].source);
    good &= gate_dos_fix_intro_rtl_offs((uint16_t)allocated, 0x1009u) == -1;
    good &= gate_dos_fix_intro_rtl_offs((uint16_t)allocated, 0x100au) == 0;
    for (i = 0u; i < sizeof expected_offsets / sizeof expected_offsets[0]; ++i)
        good &= gate_ds_word(expected_offsets[i].site) == expected_offsets[i].expected;
    for (i = 0u; i < sizeof observed_cs_sites / sizeof observed_cs_sites[0]; ++i)
        good &= gate_ds_word(observed_cs_sites[i]) == 0x1234u;
    good &= gate_ds_word(0x4d82u) == (uint16_t)allocated;
    good &= gate_ds_word(0x4d84u) == (uint16_t)(allocated + 10u);
    good &= gate_far_byte(gate_ds_segment(), 0x3592u) == pattern(0x41e6u);
    gate_dos_bind_ds(0u);
    if (_dos_freemem(allocated) != 0)
        good = 0;
    if (remove(source_name) != 0)
        good = 0;
    if (remove(rtl_source_name) != 0)
        good = 0;
    puts(good ? "Static EXE/RTL data probe: PASS (5976 + 13714 bytes; 27 seg + 24 off + 19 external pointers)"
              : "Static EXE/RTL data probe: FAIL");
    return good ? 0 : 1;
}
