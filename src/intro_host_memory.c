/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "gate_exe.h"
#include "gate_intro_host_memory.h"

#include <stdlib.h>
#include <string.h>

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

void gate_intro_memory_notify_frame(void)
{
    if (active_memory && active_memory->frame_ready)
        active_memory->frame_ready(active_memory->frame_userdata);
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
    uint8_t *address = location(segment, offset);
    if (active_memory->input_reads &&
        !(segment >= 0xe000 && segment <= 0xefff)) {
        uint32_t linear = (((uint32_t)segment << 4) + offset) & 0xfffffu;
        if (!active_memory->written_bytes[linear])
            active_memory->input_reads[linear] = 1;
    }
    return *address;
}

uint16_t gate_far_word(uint16_t segment, uint16_t offset)
{
    uint16_t low = gate_far_byte(segment, offset);
    uint16_t high = gate_far_byte(segment, (uint16_t)(offset + 1u));
    return (uint16_t)(low | (high << 8));
}

void gate_far_set_byte(uint16_t segment, uint16_t offset, uint8_t value)
{
    uint8_t *address = location(segment, offset);
    if (active_memory->written_bytes &&
        !(segment >= 0xe000 && segment <= 0xefff)) {
        uint32_t linear = (((uint32_t)segment << 4) + offset) & 0xfffffu;
        active_memory->written_bytes[linear] = 1;
    }
    *address = value;
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
    if (bytes > IMAGE_HEAP_END - IMAGE_HEAP_FIRST)
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

/* The VGA mode-12h A000 aperture is a 64KiB bank selected by sequencer
 * plane masks and graphics-controller read state. It is not ordinary RAM:
 * retain each physical plane rather than aliasing all four at A000:xxxx.
 * The VFX host models the register-driven raster operation over these
 * palette indices, and a future SDL presenter can read the same pixels. */
uint8_t gate_intro_vga16_pixel(uint16_t x, uint16_t y)
{
    uint32_t offset;
    uint8_t bit;
    uint8_t color = 0;
    unsigned plane;
    if (active_memory == 0 || x >= 640u || y >= 480u)
        abort();
    offset = (uint32_t)y * 80u + x / 8u;
    bit = (uint8_t)(0x80u >> (x & 7u));
    for (plane = 0; plane < 4; ++plane)
        if ((active_memory->vga_planes[plane][offset] & bit) != 0)
            color |= (uint8_t)(1u << plane);
    return color;
}

void gate_intro_vga16_set_pixel(uint16_t x, uint16_t y, uint8_t index)
{
    uint32_t offset;
    uint8_t bit;
    unsigned plane;
    if (active_memory == 0 || x >= 640u || y >= 480u || index > 15u)
        abort();
    offset = (uint32_t)y * 80u + x / 8u;
    bit = (uint8_t)(0x80u >> (x & 7u));
    for (plane = 0; plane < 4; ++plane) {
        uint8_t *value = &active_memory->vga_planes[plane][offset];
        if ((index & (1u << plane)) != 0)
            *value |= bit;
        else
            *value &= (uint8_t)~bit;
    }
}

void gate_intro_vga16_fill_bytes(uint16_t offset, uint16_t count, uint8_t index)
{
    unsigned plane;
    if (active_memory == 0 || index > 15u ||
        (uint32_t)offset + count > 0x10000u)
        abort();
    for (plane = 0; plane < 4; ++plane)
        memset(&active_memory->vga_planes[plane][offset],
               (index & (1u << plane)) ? 0xff : 0, count);
}
