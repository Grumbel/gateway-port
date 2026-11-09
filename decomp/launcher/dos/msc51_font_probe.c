/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Exercise the actual staged gate_rtl_font.c object (M13.OBJ), not a
 * separately written DOS implementation of the font-cache cleanup.
 * Unused dependencies of font selection are fail-closed probe-only exports.
 */
#include "GATEMEM.H"
#include "RTLLAY.H"
#include <dos.h>
#include <stdio.h>
#include <stdlib.h>

void gate_rtl_clear_font_cache(void);
void gate_rtl_release_font_slot(GateFarPtr font);
GateFarPtr gate_linked_allocate_far_heap(uint16_t bytes);
void gate_linked_free_far_heap(GateFarPtr pointer);

static void wrong_branch(void)
{
    fputs("Font probe: entered untested font-selection dependency\n", stderr);
    exit(3);
}

uint16_t gate_rtl_open_resource_file(uint16_t type, uint16_t resource)
{
    (void)type; (void)resource;
    wrong_branch();
    return 0;
}

uint16_t gate_rtl_read_resource_byte(uint16_t handle)
{
    (void)handle;
    wrong_branch();
    return 0;
}

uint16_t gate_rtl_read_resource_bytes(uint16_t handle, GateFarPtr destination, uint16_t count)
{
    (void)handle; (void)destination; (void)count;
    wrong_branch();
    return 0;
}

void gate_rtl_close_resource_file(uint16_t handle)
{
    (void)handle;
    wrong_branch();
}

uint16_t gate_rtl_font_line_advance(void)
{
    wrong_branch();
    return 0;
}

uint16_t gate_rtl_font_character_advance(uint16_t character)
{
    (void)character;
    wrong_branch();
    return 0;
}

static int valid = 1;

static void check(uint16_t actual, uint16_t expected, const char *label)
{
    if (actual != expected) {
        printf("Font probe: %s got %04x expected %04x\n", label, actual, expected);
        valid = 0;
    }
}

static void set_slot_pointer(uint16_t slot, uint16_t displacement, GateFarPtr pointer)
{
    gate_ds_set_word((uint16_t)(slot + displacement), pointer.offset);
    gate_ds_set_word((uint16_t)(slot + displacement + 2u), pointer.segment);
}

static uint16_t heap_owner(GateFarPtr pointer)
{
    /* DOS MCB immediately precedes each segment returned by allocmem. */
    return gate_far_word((uint16_t)(pointer.segment - 1u), 1u);
}

int main(void)
{
    unsigned data_segment = 0;
    GateFarPtr width, spacing, bitmap, sentinel, empty_slot;
    uint16_t first = GATE_DS_FONT_CACHE_BASE;
    uint16_t second = (uint16_t)(first + GATE_FONT_SLOT_BYTES);
    uint16_t field;

    if (_dos_allocmem(0x0560u, &data_segment) != 0) {
        puts("Font probe: cannot allocate 64KiB-relative original DS window");
        return 1;
    }
    gate_dos_bind_ds((uint16_t)data_segment);

    for (field = first; field < (uint16_t)(second + GATE_FONT_SLOT_BYTES); ++field)
        gate_far_set_byte((uint16_t)data_segment, field, 0);
    gate_ds_set_word(first, 1u);
    gate_far_set_byte((uint16_t)data_segment, (uint16_t)(first + 2u), 5u);
    gate_far_set_byte((uint16_t)data_segment, (uint16_t)(first + 3u), 9u);
    gate_ds_set_word(second, 0u);
    gate_far_set_byte((uint16_t)data_segment, (uint16_t)(second + 2u), 0x23u);
    gate_far_set_byte((uint16_t)data_segment, (uint16_t)(second + 3u), 0x46u);

    width = gate_linked_allocate_far_heap(0x20u);
    spacing = gate_linked_allocate_far_heap(0x20u);
    bitmap = gate_linked_allocate_far_heap(0x20u);
    sentinel = gate_linked_allocate_far_heap(0x20u);
    if (!width.segment || !spacing.segment || !bitmap.segment || !sentinel.segment) {
        puts("Font probe: cannot allocate original font tables");
        return 1;
    }
    set_slot_pointer(first, 0x0eu, width);
    set_slot_pointer(first, 0x12u, spacing);
    set_slot_pointer(first, 0x1au, bitmap);
    set_slot_pointer(second, 0x0eu, sentinel);

    gate_ds_set_word(GATE_DS_ACTIVE_FONT_OFFSET, 0xdeadu);
    gate_ds_set_word(GATE_DS_ACTIVE_FONT_SEGMENT, 0xbeefu);
    check(heap_owner(width) != 0, 1u, "width was owned");
    check(heap_owner(spacing) != 0, 1u, "spacing was owned");
    check(heap_owner(bitmap) != 0, 1u, "bitmap was owned");
    gate_rtl_clear_font_cache();
    check(gate_ds_word(first), 0u, "occupied refcount cleared");
    check(gate_far_byte((uint16_t)data_segment, (uint16_t)(first + 2u)), 0xffu,
          "resource mode invalidated");
    check(gate_far_byte((uint16_t)data_segment, (uint16_t)(first + 3u)), 0xffu,
          "resource id invalidated");
    check(heap_owner(width), 0u, "width MCB freed");
    check(heap_owner(spacing), 0u, "spacing MCB freed");
    check(heap_owner(bitmap), 0u, "bitmap MCB freed");
    check(heap_owner(sentinel) != 0, 1u, "empty slot skipped heap free");
    check(gate_far_byte((uint16_t)data_segment, (uint16_t)(second + 2u)), 0x23u,
          "empty resource mode preserved");
    check(gate_far_byte((uint16_t)data_segment, (uint16_t)(second + 3u)), 0x46u,
          "empty resource id preserved");
    check(gate_ds_word(GATE_DS_ACTIVE_FONT_OFFSET), second,
          "empty second slot becomes active");
    check(gate_ds_word(GATE_DS_ACTIVE_FONT_SEGMENT), (uint16_t)data_segment,
          "active font segment");

    empty_slot.segment = (uint16_t)data_segment;
    empty_slot.offset = first;
    gate_rtl_release_font_slot(empty_slot);
    check(gate_ds_word(GATE_DS_ACTIVE_FONT_OFFSET), first,
          "explicit empty slot becomes active");
    gate_linked_free_far_heap(sentinel);
    check((uint16_t)_dos_freemem(data_segment), 0u, "release original DS window");
    puts(valid ? "Font cache DOS probe: PASS" : "Font cache DOS probe: FAIL");
    return valid ? 0 : 1;
}
