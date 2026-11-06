/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gate_rtl_image.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const GateFarPtr base = {0x4545, 0x0300};
static uint8_t *stack_records;
static uint8_t guest_records[0x1100];
static char events[300];
static unsigned event_count;
static uint16_t loaded_ids[256];
static uint16_t loaded_modes[256];
static unsigned attempted, draws, releases, initializers, diagnostics;
static unsigned fail_after;
static uint16_t loader_flag;

static void note(char ch)
{
    assert(event_count < sizeof events);
    events[event_count++] = ch;
}

static void reset(void)
{
    memset(guest_records, 0xa5, sizeof guest_records);
    memset(loaded_ids, 0, sizeof loaded_ids);
    memset(loaded_modes, 0, sizeof loaded_modes);
    memset(events, 0, sizeof events);
    event_count = attempted = draws = releases = initializers = diagnostics = 0;
    fail_after = 256;
    loader_flag = 0;
    stack_records = 0;
}

static size_t index_for(GateFarPtr pointer)
{
    size_t index;
    assert(pointer.segment == base.segment);
    index = (uint16_t)(pointer.offset - base.offset);
    assert(index < sizeof guest_records);
    return index;
}

GateFarPtr gate_stack_pointer(void *address)
{
    assert(address);
    stack_records = address;
    return base;
}

uint16_t gate_rtl_image_data_segment(void) { return 0xaabb; }

void gate_far_1258_172e(GateFarPtr message)
{
    assert(message.segment == 0xaabb && message.offset == 0x14b2);
    ++diagnostics;
    note('E');
}

void gate_rtl_resource_ds_set_word(uint16_t offset, uint16_t value)
{
    assert(offset == 0x0e && value <= 1);
    loader_flag = value;
    note(value == 0 ? '-' : '+');
}

uint16_t gate_rtl_12c33(GateFarPtr pointer, uint16_t resource, uint16_t mode)
{
    const size_t index = index_for(pointer);
    assert(attempted < 256);
    assert(index == attempted * 0x88u);
    loaded_ids[attempted] = resource;
    loaded_modes[attempted] = mode;
    note('L');
    if (attempted++ == fail_after)
        return 0;
    assert(index < sizeof guest_records);
    guest_records[index] = 1;
    return 1;
}

void gate_rtl_13191(GateFarPtr pointer, int16_t count)
{
    int i;
    note('I');
    ++initializers;
    for (i = 0; i < count; ++i) {
        guest_records[index_for(pointer)] = 0;
        pointer.offset = (uint16_t)(pointer.offset + 0x88u);
    }
}

void gate_rtl_13115(GateFarPtr pointer, int16_t count)
{
    int i;
    note('D');
    for (i = 0; i < count; ++i) {
        assert(guest_records[index_for(pointer)] == 1);
        ++draws;
        pointer.offset = (uint16_t)(pointer.offset + 0x88u);
    }
}

void gate_rtl_131f7(GateFarPtr pointer, int16_t count)
{
    int i;
    note('R');
    for (i = 0; i < count; ++i) {
        const size_t index = index_for(pointer);
        assert(guest_records[index] == 1);
        guest_records[index] = 0;
        ++releases;
        pointer.offset = (uint16_t)(pointer.offset + 0x88u);
    }
}

static void test_loader_consecutive_resources(void)
{
    reset();
    assert(gate_rtl_12d67(base, 3, 0xffff, 7) == 3);
    assert(event_count == 4 && memcmp(events, "LLL-", 4) == 0);
    assert(loaded_ids[0] == 0xffff && loaded_ids[1] == 0 && loaded_ids[2] == 1);
    assert(loaded_modes[0] == 7 && loaded_modes[1] == 7);
    assert(!loader_flag);

    reset();
    fail_after = 1;
    assert(gate_rtl_12d67(base, 4, 0x1234, 9) == 1);
    assert(event_count == 3 && memcmp(events, "LL-", 3) == 0);
    assert(attempted == 2 && loaded_ids[1] == 0x1235);

    reset();
    fail_after = 0;
    assert(gate_rtl_12d67(base, 0, 0x2345, 1) == 0);
    assert(event_count == 3 && memcmp(events, "+L-", 3) == 0);
    assert(!loader_flag);
}

static void test_playback_and_early_failure(void)
{
    reset();
    gate_rtl_130ab(0x3010, 3);
    assert(event_count == 7 && memcmp(events, "ILLL-DR", 7) == 0);
    assert(initializers == 1 && attempted == 3);
    assert(draws == 3 && releases == 3 && diagnostics == 0);
    assert(stack_records != 0);
    assert(guest_records[0] == 0 && guest_records[0x88] == 0);

    reset();
    fail_after = 2;
    gate_rtl_130ab(0x30fe, 5);
    assert(event_count == 7 && memcmp(events, "ILLL-DR", 7) == 0);
    assert(attempted == 3 && draws == 2 && releases == 2);
    assert(loaded_ids[0] == 0x30fe && loaded_ids[2] == 0x3100);
    assert(loaded_modes[0] == 1);
}

static void test_over_limit_and_zero_count(void)
{
    reset();
    gate_rtl_130ab(0x3001, 33);
    assert(event_count == 1 && events[0] == 'E');
    assert(!attempted && !draws && !releases && !initializers);
    assert(stack_records == 0);

    reset();
    fail_after = 1;
    gate_rtl_130ab(0x3001, 0);
    assert(event_count == 7 && memcmp(events, "I+LL-DR", 7) == 0);
    assert(attempted == 2 && draws == 1 && releases == 1);
    assert(!loader_flag);
}

int main(void)
{
    test_loader_consecutive_resources();
    test_playback_and_early_failure();
    test_over_limit_and_zero_count();
    puts("original intro array loading, playback and oversized-count guard: OK");
    return 0;
}
