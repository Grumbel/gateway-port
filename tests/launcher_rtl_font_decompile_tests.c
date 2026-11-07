/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gate_rtl.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { DS = 0x1000, TYPE_SEG = 0x2000, VIDEO_SEG = 0x2100,
       MEM_SEG = 0x3000, ALLOC_SEG = 0x4000 };
static uint8_t ram[0x80000];
static uint8_t contents[270];
static uint16_t opened_type, opened_resource;
static unsigned opens, closes, reads, frees, allocations, failure_at;
static unsigned failure_once;
static uint16_t next_allocation;
static uint16_t ink_width;

static unsigned linear(uint16_t segment, uint16_t offset)
{
    return (unsigned)segment * 16u + offset;
}

uint16_t gate_ds_segment(void) { return DS; }
uint8_t gate_far_byte(uint16_t segment, uint16_t offset)
{
    return ram[linear(segment, offset)];
}
uint16_t gate_far_word(uint16_t segment, uint16_t offset)
{
    return (uint16_t)(gate_far_byte(segment, offset) |
                      ((uint16_t)gate_far_byte(segment, (uint16_t)(offset + 1)) << 8));
}
void gate_far_set_byte(uint16_t segment, uint16_t offset, uint8_t value)
{
    ram[linear(segment, offset)] = value;
}
void gate_far_set_word(uint16_t segment, uint16_t offset, uint16_t value)
{
    gate_far_set_byte(segment, offset, (uint8_t)value);
    gate_far_set_byte(segment, (uint16_t)(offset + 1), (uint8_t)(value >> 8));
}
uint16_t gate_ds_word(uint16_t offset) { return gate_far_word(DS, offset); }
void gate_ds_set_word(uint16_t offset, uint16_t value)
{
    gate_far_set_word(DS, offset, value);
}

GateFarPtr gate_linked_allocate_far_heap(uint16_t bytes)
{
    GateFarPtr result;
    ++allocations;
    if (failure_at != 0 && allocations >= failure_at &&
        (!failure_once || allocations == failure_at))
        return (GateFarPtr){0, 0};
    result = (GateFarPtr){ALLOC_SEG, next_allocation};
    next_allocation = (uint16_t)(next_allocation + bytes + 16);
    return result;
}
void gate_linked_free_far_heap(GateFarPtr pointer)
{
    (void)pointer;
    ++frees;
}

uint16_t gate_rtl_open_resource_file(uint16_t type, uint16_t resource)
{
    ++opens;
    opened_type = type;
    opened_resource = resource;
    reads = 0;
    return type == 2 && resource == 305 ? 5 : 0;
}
uint16_t gate_rtl_read_resource_byte(uint16_t handle)
{
    assert(handle == 5 && reads < sizeof contents);
    return contents[reads++];
}
uint16_t gate_rtl_read_resource_bytes(uint16_t handle, GateFarPtr destination, uint16_t count)
{
    uint16_t i;
    assert(handle == 5 && reads + count <= sizeof contents);
    for (i = 0; i < count; ++i)
        gate_far_set_byte(destination.segment, (uint16_t)(destination.offset + i),
                          contents[reads++]);
    return count;
}
void gate_rtl_close_resource_file(uint16_t handle)
{
    assert(handle == 5);
    ++closes;
}
uint16_t gate_rtl_font_character_advance(uint16_t ch)
{
    assert(ch == 'M');
    return ink_width;
}
uint16_t gate_rtl_font_line_advance(void)
{
    GateFarPtr p = {gate_ds_word(0x54cc), gate_ds_word(0x54ca)};
    return (uint16_t)((int8_t)gate_far_byte(p.segment, (uint16_t)(p.offset + 9)) +
                      (int8_t)gate_far_byte(p.segment, (uint16_t)(p.offset + 10)));
}

static void setup(void)
{
    unsigned i;
    memset(ram, 0, sizeof ram);
    memset(contents, 0, sizeof contents);
    opens = closes = reads = frees = allocations = failure_at = failure_once = 0;
    next_allocation = 0x100;
    ink_width = 15;
    gate_ds_set_word(0x4d84, TYPE_SEG);
    gate_ds_set_word(0x4d86, VIDEO_SEG);
    gate_ds_set_word(0x4d88, MEM_SEG);
    gate_ds_set_word(0x4d8a, MEM_SEG);
    gate_ds_set_word(0x4d8c, MEM_SEG);
    gate_ds_set_word(0x4d8e, MEM_SEG);
    gate_far_set_word(TYPE_SEG, 0x0fce, 3);
    gate_far_set_byte(VIDEO_SEG, 0x003c, 16);
    /* An independent tiny 10-byte FNT header, two 128-byte tables and a
     * four-byte bitmap. The contents are synthetic, not a game asset. */
    contents[2] = 1;    /* bytes per bitmap row */
    contents[3] = 'A';
    contents[4] = 'B';
    contents[5] = 1;
    contents[6] = 2;
    contents[7] = 0xff; /* widths present */
    contents[8] = 0xff; /* spacing present */
    for (i = 0; i < 128; ++i) {
        contents[10 + i] = (uint8_t)i;
        contents[138 + i] = (uint8_t)(127 - i);
    }
    for (i = 0; i < 4; ++i)
        contents[266 + i] = (uint8_t)(0xa0 + i);
    /* An empty cache slot must not accidentally alias a requested font. */
    gate_far_set_byte(DS, 0x54ce + 2, 0xff);
    gate_far_set_byte(DS, 0x54ce + 3, 0xff);
    gate_far_set_byte(DS, 0x54ce + 30 + 2, 0xff);
    gate_far_set_byte(DS, 0x54ce + 30 + 3, 0xff);
}

static void test_real_resource_branch_and_cache_hit(void)
{
    GateFarPtr slot, again;
    GateFarPtr widths, spacing, bitmap;
    setup();
    slot = gate_rtl_select_font(5);
    assert(slot.segment == DS && slot.offset == 0x54ce);
    assert(opened_type == 2 && opened_resource == 305);
    assert(opens == 1 && closes == 1 && reads == sizeof contents);
    assert(allocations == 3 && frees == 0);
    assert(gate_ds_word(0x54ca) == slot.offset);
    assert(gate_far_word(DS, slot.offset) == 1);
    assert(gate_far_byte(DS, slot.offset + 2) == 3);
    assert(gate_far_byte(DS, slot.offset + 3) == 5);
    widths = (GateFarPtr){gate_far_word(DS, slot.offset + 16),
                          gate_far_word(DS, slot.offset + 14)};
    spacing = (GateFarPtr){gate_far_word(DS, slot.offset + 20),
                           gate_far_word(DS, slot.offset + 18)};
    bitmap = (GateFarPtr){gate_far_word(DS, slot.offset + 28),
                          gate_far_word(DS, slot.offset + 26)};
    assert(gate_far_byte(widths.segment, widths.offset + 'M') == 'M');
    assert(gate_far_byte(spacing.segment, spacing.offset + 'M') == 127 - 'M');
    assert(gate_far_byte(bitmap.segment, bitmap.offset + 3) == 0xa3);
    assert(gate_far_word(MEM_SEG, 0x32) == 15);
    assert(gate_far_word(MEM_SEG, 0x34) == 3);
    assert(gate_far_word(MEM_SEG, 0x36) == 7);
    assert(gate_far_word(MEM_SEG, 0x38) == 1);

    /* The second call increments both occupied counters, then resets the
     * matching slot's count to one without reopening its FNT file. */
    again = gate_rtl_select_font(0x105);
    assert(again.offset == slot.offset);
    assert(gate_far_word(DS, slot.offset) == 1);
    assert(opens == 1 && closes == 1 && allocations == 3 && frees == 0);
}

static void test_eviction_and_missing_resource_fallback(void)
{
    GateFarPtr second, selected;
    setup();
    (void)gate_rtl_select_font(5);
    second = gate_rtl_select_font(9); /* resource 309 is absent */
    assert(second.offset == 0x54ec);
    assert(opened_resource == 309 && opens == 2 && closes == 1);
    assert(gate_far_byte(DS, second.offset + 11) == 8);
    assert(gate_far_byte(DS, second.offset + 10) == 16);
    assert(gate_far_word(DS, 0x54ce) == 2);
    assert(gate_far_word(DS, second.offset) == 1);
    selected = gate_rtl_select_font(7);
    assert(selected.offset == second.offset); /* lower reference count */
    assert(frees == 3 && opens == 3);
    assert(gate_far_byte(DS, second.offset + 3) == 7);
    assert(gate_far_word(DS, 0x54ce) == 3);
    gate_rtl_clear_font_cache();
    assert(frees == 9);
    assert(gate_far_word(DS, 0x54ce) == 0);
    assert(gate_far_word(DS, 0x54ec) == 0);
    assert(gate_ds_word(0x54ca) == 0x54ec);
}

static void test_retry_then_default_on_exhausted_heap(void)
{
    GateFarPtr slot;
    setup();
    /* Force an allocation failure in each attempt at the same resource.
     * After two failed opens, the original falls back to font ID zero. */
    failure_at = 1;
    slot = gate_rtl_select_font(5);
    assert(slot.offset == 0x54ce);
    assert(opens == 2 && closes == 2);
    assert(gate_far_byte(DS, slot.offset + 3) == 0);
    assert(gate_far_byte(DS, slot.offset + 11) == 8);
    assert(frees == 6);
    assert(allocations == 2); /* both allocation attempts failed */
}

static void test_retry_succeeds_and_negative_metric_rounding(void)
{
    GateFarPtr slot;
    setup();
    failure_at = 2;
    failure_once = 1; /* second allocation fails only on first pass */
    ink_width = 0xfffd; /* signed -3, half rounds toward zero to -1 */
    slot = gate_rtl_select_font(5);
    assert(slot.offset == 0x54ce);
    assert(opens == 2 && closes == 2);
    assert(allocations == 5 && frees == 3);
    assert(gate_far_byte(DS, slot.offset + 3) == 5);
    gate_far_set_byte(DS, (uint16_t)(slot.offset + 9), 0xfe);
    gate_far_set_byte(DS, (uint16_t)(slot.offset + 10), 0xfd);
    (void)gate_rtl_select_font(5); /* cached descriptor, signed metrics now -5 */
    assert(gate_far_word(MEM_SEG, 0x32) == 0xfffd);
    assert(gate_far_word(MEM_SEG, 0x34) == 0xfffb);
    assert(gate_far_word(MEM_SEG, 0x36) == 0xffff);
    assert(gate_far_word(MEM_SEG, 0x38) == 0xfffe);
}

int main(void)
{
    test_real_resource_branch_and_cache_hit();
    test_eviction_and_missing_resource_fallback();
    test_retry_then_default_on_exhausted_heap();
    test_retry_succeeds_and_negative_metric_rounding();
    puts("original RTL font cache, file loading and fallback: OK");
    return 0;
}
