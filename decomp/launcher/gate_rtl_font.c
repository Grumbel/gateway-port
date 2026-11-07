/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_rtl.h"
#include "gate_rtl_file.h"

/* Original GATE.RTL:e771..ebcf, linked at 0c4d:018d in the observed intro.
 * The original stores two 30-byte slots in its own DS and accesses the
 * current video/font type through relocated segment-selector words. */
static GateFarPtr font_slot(uint16_t index)
{
    return (GateFarPtr){gate_ds_segment(),
                        (uint16_t)(GATE_DS_FONT_CACHE_BASE + index * GATE_FONT_SLOT_BYTES)};
}

static uint16_t font_word(GateFarPtr font, uint16_t field)
{
    return gate_far_word(font.segment, (uint16_t)(font.offset + field));
}

static uint8_t font_byte(GateFarPtr font, uint16_t field)
{
    return gate_far_byte(font.segment, (uint16_t)(font.offset + field));
}

static void font_set_byte(GateFarPtr font, uint16_t field, uint8_t value)
{
    gate_far_set_byte(font.segment, (uint16_t)(font.offset + field), value);
}

static void font_set_word(GateFarPtr font, uint16_t field, uint16_t value)
{
    gate_far_set_word(font.segment, (uint16_t)(font.offset + field), value);
}

static GateFarPtr font_pointer(GateFarPtr font, uint16_t field)
{
    return (GateFarPtr){font_word(font, (uint16_t)(field + 2)),
                        font_word(font, field)};
}

static void font_set_pointer(GateFarPtr font, uint16_t field, GateFarPtr value)
{
    font_set_word(font, field, value.offset);
    font_set_word(font, (uint16_t)(field + 2), value.segment);
}

static void font_set_active(GateFarPtr font)
{
    gate_ds_set_word(GATE_DS_ACTIVE_FONT_OFFSET, font.offset);
    gate_ds_set_word(GATE_DS_ACTIVE_FONT_SEGMENT, font.segment);
}

/* The live RTL linkage uses DS:4d84 -> [ES:0fce] and DS:4d86 ->
 * [ES:003c]. They are not interchangeable: the former is the resource
 * type (multiplied by 100), the latter is the fallback character height. */
static uint16_t font_resource_type(void)
{
    return gate_far_word(gate_ds_word(GATE_DS_FONT_RESOURCE_MODE_SEGMENT), GATE_DS_GRAPHICS_MODE);
}

static uint8_t font_fallback_height(void)
{
    return gate_far_byte(gate_ds_word(GATE_DS_FONT_LINE_PITCH_SEGMENT), GATE_MODE_DESC_TEXT_LINE_PITCH);
}

/* RTL:e79b..e800: release one slot if its reference counter is nonzero.
 * The three far pointers are passed to the allocator's free service even
 * when no corresponding optional width/spacing table was ever allocated. */
void gate_rtl_release_font_slot(GateFarPtr font)
{
    if (font_word(font, GATE_FONT_SLOT_REFCOUNT) == 0)
        return;
    font_set_word(font, GATE_FONT_SLOT_REFCOUNT, 0);
    font_set_byte(font, GATE_FONT_SLOT_RESOURCE_MODE, 0xff);
    font_set_byte(font, GATE_FONT_SLOT_RESOURCE_ID, 0xff);
    gate_linked_free_far_heap(font_pointer(font, GATE_FONT_SLOT_WIDTH_TABLE_OFFSET));
    gate_linked_free_far_heap(font_pointer(font, GATE_FONT_SLOT_SPACING_TABLE_OFFSET));
    gate_linked_free_far_heap(font_pointer(font, GATE_FONT_SLOT_BITMAP_OFFSET));
    font_set_active(font);
}

/* RTL:e771..e79a: reset both cache slots in increasing slot order. */
void gate_rtl_clear_font_cache(void)
{
    uint16_t i;
    for (i = 0; i < GATE_FONT_SLOT_COUNT; ++i)
        gate_rtl_release_font_slot(font_slot(i));
}

/* RTL:e801..e872: select an unused slot, or the slot with the lowest
 * signed reference count. Its unusual early exit when slot zero is empty
 * is preserved. The chosen slot is released before returning. */
GateFarPtr gate_rtl_choose_font_slot(void)
{
    GateFarPtr candidate = font_slot(0);
    int16_t smallest = (int16_t)font_word(candidate, 0);
    uint16_t i = 1;
    while (smallest > 0) {
        GateFarPtr other;
        int16_t count;
        if (i >= GATE_FONT_SLOT_COUNT)
            break;
        other = font_slot(i);
        count = (int16_t)font_word(other, 0);
        if (count == 0 || smallest >= count) {
            candidate = other;
            smallest = count;
        }
        ++i;
    }
    gate_rtl_release_font_slot(candidate);
    return candidate;
}

/* CWD; SUB AX,DX; SAR AX,1 in the original rounds negative odd values
 * toward zero, unlike a plain arithmetic right shift. */
static uint16_t font_half(uint16_t value)
{
    value = (uint16_t)(value - ((value & 0x8000u) ? 0xffffu : 0u));
    return (uint16_t)((value >> 1) | (value & 0x8000u));
}

static void font_update_metrics(void)
{
    uint16_t width = gate_rtl_font_character_advance('M');
    uint16_t height = gate_rtl_font_line_advance();
    uint16_t width_segment = gate_ds_word(GATE_DS_FONT_WIDTH_SEGMENT);
    uint16_t height_segment = gate_ds_word(GATE_DS_FONT_HEIGHT_SEGMENT);
    gate_far_set_word(width_segment, GATE_MODE_DESC_FONT_WIDTH, width);
    gate_far_set_word(height_segment, GATE_MODE_DESC_FONT_HEIGHT, height);
    gate_far_set_word(gate_ds_word(GATE_DS_FONT_HALF_WIDTH_SEGMENT), GATE_MODE_DESC_FONT_HALF_WIDTH, font_half(width));
    gate_far_set_word(gate_ds_word(GATE_DS_FONT_HALF_HEIGHT_SEGMENT), GATE_MODE_DESC_FONT_HALF_HEIGHT, font_half(height));
}

/* RTL:e873..ebcf, linked FAR 0c4d:018d. Only the low byte of the request
 * participates in cache identity, but its entire WORD participates in the
 * 16-bit resource ID. Read counts are ignored by the original; short file
 * data leaves the remaining memory unmodified. */
GateFarPtr gate_rtl_select_font(uint16_t request)
{
    GateFarPtr font;
    uint16_t i;
    uint16_t handle;
    uint16_t retried = 0;

    for (i = 0; i < GATE_FONT_SLOT_COUNT; ++i) {
        font = font_slot(i);
        if (font_word(font, GATE_FONT_SLOT_REFCOUNT) != 0)
            font_set_word(font, GATE_FONT_SLOT_REFCOUNT,
                          (uint16_t)(font_word(font, GATE_FONT_SLOT_REFCOUNT) + 1));
    }

    for (i = 0; i < GATE_FONT_SLOT_COUNT; ++i) {
        font = font_slot(i);
        if (font_byte(font, GATE_FONT_SLOT_RESOURCE_MODE) == (uint8_t)font_resource_type() &&
            font_byte(font, GATE_FONT_SLOT_RESOURCE_ID) == (uint8_t)request) {
            font_set_word(font, GATE_FONT_SLOT_REFCOUNT, 1);
            font_set_active(font);
            goto update_metrics;
        }
    }

    font = gate_rtl_choose_font_slot();
    font_set_active(font);

retry:
    handle = request != 0
        ? gate_rtl_open_resource_file(2, (uint16_t)(font_resource_type() * 100u + request))
        : 0;
    font_set_word(font, GATE_FONT_SLOT_REFCOUNT, 1);
    font_set_byte(font, GATE_FONT_SLOT_RESOURCE_MODE, (uint8_t)font_resource_type());
    font_set_byte(font, GATE_FONT_SLOT_RESOURCE_ID, (uint8_t)request);

    if (request == 0 || handle == 0) {
        font_set_byte(font, GATE_FONT_SLOT_HEADER_UNKNOWN_04, 0);
        font_set_byte(font, GATE_FONT_SLOT_HEADER_UNKNOWN_05, 0);
        font_set_byte(font, GATE_FONT_SLOT_GLYPH_ROW_BYTES, 0);
        font_set_byte(font, GATE_FONT_SLOT_FIRST_CHARACTER, 0);
        font_set_byte(font, GATE_FONT_SLOT_LAST_CHARACTER, 0xff);
        font_set_byte(font, GATE_FONT_SLOT_LINE_GAP, 0);
        font_set_byte(font, GATE_FONT_SLOT_GLYPH_HEIGHT, font_fallback_height());
        font_set_byte(font, GATE_FONT_SLOT_WIDTH_MODE, 8);
        font_set_byte(font, GATE_FONT_SLOT_SPACING_MODE, 0);
        font_set_byte(font, GATE_FONT_SLOT_HEADER_UNKNOWN_0D, 0);
        goto update_metrics;
    }

    for (i = GATE_FONT_SLOT_HEADER_FIRST_BYTE; i <= GATE_FONT_SLOT_HEADER_LAST_BYTE; ++i)
        font_set_byte(font, i, (uint8_t)gate_rtl_read_resource_byte(handle));

    if ((int8_t)font_byte(font, GATE_FONT_SLOT_WIDTH_MODE) < 0) {
        GateFarPtr widths = gate_linked_allocate_far_heap(GATE_FONT_SLOT_LOOKUP_TABLE_BYTES);
        font_set_pointer(font, GATE_FONT_SLOT_WIDTH_TABLE_OFFSET, widths);
        if ((uint16_t)(widths.segment | widths.offset) == 0)
            goto allocation_failed;
        (void)gate_rtl_read_resource_bytes(handle, widths, GATE_FONT_SLOT_LOOKUP_TABLE_BYTES);
    }
    if ((int8_t)font_byte(font, GATE_FONT_SLOT_SPACING_MODE) < 0) {
        GateFarPtr spacing = gate_linked_allocate_far_heap(GATE_FONT_SLOT_LOOKUP_TABLE_BYTES);
        font_set_pointer(font, GATE_FONT_SLOT_SPACING_TABLE_OFFSET, spacing);
        if ((uint16_t)(spacing.segment | spacing.offset) == 0)
            goto allocation_failed;
        (void)gate_rtl_read_resource_bytes(handle, spacing, GATE_FONT_SLOT_LOOKUP_TABLE_BYTES);
    }
    {
        uint16_t bytes = (uint16_t)(
            (uint16_t)((int16_t)(int8_t)font_byte(font, GATE_FONT_SLOT_LAST_CHARACTER) -
                       (int16_t)(int8_t)font_byte(font, GATE_FONT_SLOT_FIRST_CHARACTER) + 1) *
            (uint16_t)(int16_t)(int8_t)font_byte(font, GATE_FONT_SLOT_GLYPH_ROW_BYTES) *
            (uint16_t)(int16_t)(int8_t)font_byte(font, GATE_FONT_SLOT_GLYPH_HEIGHT));
        GateFarPtr bitmap = gate_linked_allocate_far_heap(bytes);
        font_set_pointer(font, GATE_FONT_SLOT_BITMAP_OFFSET, bitmap);
        if ((uint16_t)(bitmap.segment | bitmap.offset) == 0)
            goto allocation_failed;
        (void)gate_rtl_read_resource_bytes(handle, bitmap, bytes);
    }
    gate_rtl_close_resource_file(handle);
    goto update_metrics;

allocation_failed:
    gate_rtl_clear_font_cache();
    gate_rtl_close_resource_file(handle);
    if (retried == 0) {
        retried = 1;
        goto retry; /* original retries exactly once with same resource ID */
    }
    request = 0;
    goto retry;

update_metrics:
    font_update_metrics();
    return font;
}
