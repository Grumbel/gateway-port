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
