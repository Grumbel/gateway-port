/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "gatemain_flags.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { DS = 0x1111, COUNT_SEG = 0x2222, DIRECTORY_SEG = 0x3333,
       PROPERTY_SEG = 0x4444, STATE_SEG = 0x5555 };
static uint8_t ds[65536], counts[65536], directory[65536];
static uint8_t properties[65536], state[65536];
static unsigned writes;

static uint8_t *memory(uint16_t segment)
{
    switch (segment) {
    case DS: return ds;
    case COUNT_SEG: return counts;
    case DIRECTORY_SEG: return directory;
    case PROPERTY_SEG: return properties;
    case STATE_SEG: return state;
    }
    assert(!"invalid segment");
    return 0;
}

static void put_word(uint16_t segment, uint16_t offset, uint16_t word)
{
    uint8_t *bytes = memory(segment);
    bytes[offset] = (uint8_t)word;
    bytes[(uint16_t)(offset + 1u)] = (uint8_t)(word >> 8);
}

uint16_t gatemain_ds_word(uint16_t offset)
{
    return gatemain_far_word(DS, offset);
}

uint8_t gatemain_far_byte(uint16_t segment, uint16_t offset)
{
    return memory(segment)[offset];
}

uint16_t gatemain_far_word(uint16_t segment, uint16_t offset)
{
    return (uint16_t)(gatemain_far_byte(segment, offset) |
                      ((uint16_t)gatemain_far_byte(segment,
                                                  (uint16_t)(offset + 1u)) << 8));
}

void gatemain_far_set_byte(uint16_t segment, uint16_t offset, uint8_t value)
{
    memory(segment)[offset] = value;
    ++writes;
}

static uint16_t directory_index(uint16_t object)
{
    return (uint16_t)(object * 6u + 0xbau);
}

static void configure_object(uint16_t object, uint8_t type, uint16_t offset)
{
    uint16_t index = directory_index(object);
    directory[index] = type;
    put_word(DIRECTORY_SEG, (uint16_t)(index + 2u), offset);
    put_word(DIRECTORY_SEG, (uint16_t)(index + 4u), STATE_SEG);
}

static void configure_property(uint16_t property, uint8_t bit, uint16_t mask)
{
    uint16_t index = (uint16_t)(property * 4u);
    properties[(uint16_t)(index + 0x14u)] = bit;
    put_word(PROPERTY_SEG, (uint16_t)(index + 0x16u), mask);
}

static void initialize(void)
{
    memset(ds, 0, sizeof ds);
    memset(counts, 0, sizeof counts);
    memset(directory, 0, sizeof directory);
    memset(properties, 0, sizeof properties);
    memset(state, 0, sizeof state);
    writes = 0;
    put_word(DS, 0x8d54, COUNT_SEG);
    put_word(DS, 0x8d50, DIRECTORY_SEG);
    put_word(DS, 0x8d56, PROPERTY_SEG);
    put_word(COUNT_SEG, 0x38dc, 3);
    configure_object(1, 1, 0x1000);
    configure_object(2, 8, 0xfffa);
    configure_object(3, 4, 0x1800);
    configure_property(1, 0, 0x00ff);
    configure_property(2, 9, 0x0008);  /* type 4 only */
    configure_property(3, 7, 0x00ff);
    configure_property(4, 40, 0x00ff);
}

static void test_pointer_helper_and_independent_types(void)
{
    GatemainFarPtr ptr;
    initialize();
    for (uint16_t type = 1; type <= 8; ++type) {
        configure_object(1, (uint8_t)type, 0x1000);
        ptr = gatemain_exe_27db(1);
        assert(ptr.segment == STATE_SEG && ptr.offset == 0x1004);
        /* The original eight-way jump table uses ONE destination. */
    }
    ptr = gatemain_exe_27db(0);
    assert(ptr.offset == 0 && ptr.segment == 0);
    ptr = gatemain_exe_27db(4);
    assert(ptr.offset == 0 && ptr.segment == 0);
    ptr = gatemain_exe_27db(0x8000);
    assert(ptr.offset == 0 && ptr.segment == 0);
    configure_object(1, 0, 0x1000);
    assert(gatemain_exe_27db(1).segment == 0);
    configure_object(1, 9, 0x1000);
    assert(gatemain_exe_27db(1).segment == 0);
    assert(writes == 0);
}

static void test_lookup_and_rejected_paths(void)
{
    GatemainFarPtr ptr;
    uint16_t bit;
    initialize();
    bit = 0xbeef;
    ptr = gatemain_exe_284b(3, 2, &bit);
    assert(ptr.segment == STATE_SEG && ptr.offset == 0x1805 && bit == 1);

    bit = 0xbeef;
    ptr = gatemain_exe_284b(1, 2, &bit); /* property not for type 1 */
    assert(ptr.segment == 0 && ptr.offset == 0 && bit == 0xbeef);
    ptr = gatemain_exe_284b(1, 0, &bit);
    assert(ptr.segment == 0 && ptr.offset == 0 && bit == 0xbeef);
    ptr = gatemain_exe_284b(0, 1, &bit);
    assert(ptr.segment == 0 && ptr.offset == 0 && bit == 0xbeef);
    ptr = gatemain_exe_284b(0xffff, 1, &bit);
    assert(ptr.segment == 0 && ptr.offset == 0 && bit == 0xbeef);
    ptr = gatemain_exe_284b(3, 5, &bit);
    assert(ptr.segment == 0 && ptr.offset == 0 && bit == 0xbeef);

    /* Property-index SHL SI,2 truncates to 16 bits: 0x4001 aliases 1. */
    ptr = gatemain_exe_284b(1, 0x4001, &bit);
    assert(ptr.segment == STATE_SEG && ptr.offset == 0x1004 && bit == 0);
}

static void test_queries_sets_and_clears(void)
{
    initialize();
    state[0x1004] = 0xaa;
    assert(gatemain_exe_28e4(1, 1) == 0);
    assert(gatemain_exe_28e4(1, 3) == 1); /* bit 7 */
    gatemain_exe_291e(1, 1);
    assert(state[0x1004] == 0xab && writes == 1);
    assert(gatemain_exe_28e4(1, 1) == 1);
    gatemain_exe_291e(1, 1);
    assert(state[0x1004] == 0xab && writes == 2); /* idempotent OR */
    gatemain_exe_2951(1, 3);
    assert(state[0x1004] == 0x2b && writes == 3);
    gatemain_exe_2951(1, 1);
    assert(state[0x1004] == 0x2a && writes == 4);
    gatemain_exe_291e(1, 2);
    gatemain_exe_2951(0, 1);
    assert(state[0x1004] == 0x2a && writes == 4);
    assert(gatemain_exe_28e4(1, 0) == 0);
}

static void test_far_offset_wrap_and_large_bit_index(void)
{
    GatemainFarPtr ptr;
    uint16_t bit = 0xffff;
    initialize();
    ptr = gatemain_exe_27db(2);
    assert(ptr.segment == STATE_SEG && ptr.offset == 0xfffe);
    ptr = gatemain_exe_284b(2, 4, &bit);
    assert(ptr.segment == STATE_SEG && ptr.offset == 3 && bit == 0);
    state[3] = 0xf0;
    gatemain_exe_291e(2, 4);
    assert(state[3] == 0xf1 && writes == 1);
    assert(gatemain_exe_28e4(2, 4) == 1);
    gatemain_exe_2951(2, 4);
    assert(state[3] == 0xf0);
    assert(state[0] == 0 && state[0xfffe] == 0);
}

int main(void)
{
    test_pointer_helper_and_independent_types();
    test_lookup_and_rejected_paths();
    test_queries_sets_and_clears();
    test_far_offset_wrap_and_large_bit_index();
    puts("original GATEMAIN flags C: pointer resolution, type mask, bit operations passed");
    return 0;
}
