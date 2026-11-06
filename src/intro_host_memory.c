/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "gate_exe.h"
#include "gate_intro_host_memory.h"

#include <stdlib.h>

/* This is a host implementation of the original segmented-memory boundary,
 * not an attempt to translate all 8086 hardware into portable C. The
 * synthetic segments 0xe000..0xefff are reserved for pointers to native
 * stack locals created by gate_stack_pointer(); they are not DOS RAM.
 * The observed DOS intro uses conventional RAM below this area, but any
 * guest access to physical ROM space in these segments would collide with
 * this bridge and requires a separately modeled host/hardware service.
 * This is a bridge toward independent playback, not a complete emulator. */
static GateIntroMemory *active_memory;

void gate_intro_memory_attach(GateIntroMemory *memory)
{
    active_memory = memory;
}

void gate_intro_memory_detach(void)
{
    active_memory = 0;
}

static uint8_t *location(uint16_t segment, uint16_t offset)
{
    unsigned i;
    if (active_memory == 0)
        abort();
    if (segment >= 0xe000 && segment <= 0xefff) {
        for (i = 0; i < active_memory->stack_view_count; ++i) {
            GateIntroStackView *view = &active_memory->stack_views[i];
            if (view->synthetic_segment == segment)
                return (uint8_t *)view->native_pointer + offset;
        }
        abort(); /* unknown synthetic segment is a host binding error */
    }
    return &active_memory->bytes[(((uint32_t)segment << 4) + offset) & 0xfffffu];
}

uint8_t gate_far_byte(uint16_t segment, uint16_t offset)
{
    return *location(segment, offset);
}

uint16_t gate_far_word(uint16_t segment, uint16_t offset)
{
    uint16_t low = gate_far_byte(segment, offset);
    uint16_t high = gate_far_byte(segment, (uint16_t)(offset + 1u));
    return (uint16_t)(low | (high << 8));
}

void gate_far_set_byte(uint16_t segment, uint16_t offset, uint8_t value)
{
    *location(segment, offset) = value;
}

void gate_far_set_word(uint16_t segment, uint16_t offset, uint16_t value)
{
    gate_far_set_byte(segment, offset, (uint8_t)value);
    gate_far_set_byte(segment, (uint16_t)(offset + 1u), (uint8_t)(value >> 8));
}

uint16_t gate_ds_segment(void)
{
    if (active_memory == 0)
        abort();
    return active_memory->ds_segment;
}

uint16_t gate_ds_word(uint16_t offset)
{
    return gate_far_word(gate_ds_segment(), offset);
}

void gate_ds_set_word(uint16_t offset, uint16_t value)
{
    gate_far_set_word(gate_ds_segment(), offset, value);
}

GateFarPtr gate_stack_pointer(void *ptr)
{
    unsigned i;
    GateIntroStackView *view;
    GateFarPtr result;
    if (active_memory == 0 || ptr == 0)
        abort();
    for (i = 0; i < active_memory->stack_view_count; ++i) {
        view = &active_memory->stack_views[i];
        if (view->native_pointer == ptr) {
            result.segment = view->synthetic_segment;
            result.offset = 0;
            return result;
        }
    }
    if (active_memory->stack_view_count == GATE_INTRO_MAX_STACK)
        abort();
    view = &active_memory->stack_views[active_memory->stack_view_count++];
    view->native_pointer = ptr;
    view->synthetic_segment = (uint16_t)(0xe000u +
        (active_memory->stack_view_count - 1u));
    result.segment = view->synthetic_segment;
    result.offset = 0;
    return result;
}

/* Replacement for the DOS image heap allocator used at 253a:0004.
 * Original DOS-owned code/data in this range is not executed in the native
 * source-C player. All allocations are paragraph-aligned so a FAR pointer
 * can address 64KiB in one segment. The original image routines constrain
 * a single bitmap to this offset range. */
enum { IMAGE_HEAP_FIRST = 0x60000u, IMAGE_HEAP_END = 0x9f000u };

GateFarPtr gate_intro_memory_alloc(uint32_t bytes)
{
    unsigned slot;
    uint32_t cursor;
    uint32_t size;

    if (active_memory == 0)
        abort();
    if (bytes > 0xffffu)
        return (GateFarPtr){0, 0};
    size = ((bytes == 0 ? 1u : bytes) + 15u) & ~15u;
    slot = 0;
    while (slot < GATE_INTRO_MAX_ALLOCATIONS &&
           active_memory->allocations[slot].active)
        ++slot;
    if (slot == GATE_INTRO_MAX_ALLOCATIONS)
        return (GateFarPtr){0, 0};

    cursor = IMAGE_HEAP_FIRST;
    while (cursor + size <= IMAGE_HEAP_END) {
        unsigned i;
        uint32_t end = cursor + size;
        uint32_t next = cursor;
        for (i = 0; i < GATE_INTRO_MAX_ALLOCATIONS; ++i) {
            const GateIntroAllocation *block = &active_memory->allocations[i];
            if (!block->active)
                continue;
            if (cursor < block->linear + block->length &&
                end > block->linear && next < block->linear + block->length)
                next = block->linear + block->length;
        }
        if (next == cursor) {
            GateIntroAllocation *block = &active_memory->allocations[slot];
            block->linear = cursor;
            block->length = size;
            block->active = 1;
            return (GateFarPtr){(uint16_t)(cursor >> 4), 0};
        }
        cursor = (next + 15u) & ~15u;
    }
    return (GateFarPtr){0, 0};
}

uint16_t gate_intro_memory_free(GateFarPtr pointer)
{
    unsigned i;
    uint32_t linear;
    if (active_memory == 0)
        abort();
    linear = (((uint32_t)pointer.segment << 4) + pointer.offset) & 0xfffffu;
    for (i = 0; i < GATE_INTRO_MAX_ALLOCATIONS; ++i) {
        GateIntroAllocation *block = &active_memory->allocations[i];
        if (block->active && block->linear == linear && pointer.offset == 0) {
            block->active = 0;
            return 0;
        }
    }
    return 0xffe4u; /* invalid original VFX pixel allocation */
}

/* The recovered 245d image allocator keeps the original DOS FAR service
 * addresses. These two native thunks provide its pixel allocation boundary. */
GateFarPtr gate_far_253a_0004(uint32_t bytes)
{
    return gate_intro_memory_alloc(bytes);
}

uint16_t gate_far_253a_0048(GateFarPtr pointer)
{
    return gate_intro_memory_free(pointer);
}
