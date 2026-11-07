/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "gate_rtl_image.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t *work_memory;
static GateFarPtr work_ptr;
static uint16_t load_result, seen_resource, seen_mode;
static char call_order[8];
static unsigned ncalled;

GateFarPtr gate_stack_pointer(void *buffer)
{
    work_memory = buffer;
    return (GateFarPtr){0x1234, 0xfff7};
}

static void called(char id, GateFarPtr pointer)
{
    assert(pointer.segment == work_ptr.segment);
    assert(pointer.offset == work_ptr.offset);
    assert(ncalled < sizeof call_order);
    call_order[ncalled++] = id;
}

uint16_t gate_rtl_load_picture_record(GateFarPtr work, uint16_t resource, uint16_t mode)
{
    called('L', work);
    assert(work_memory[0] == 0);
    seen_resource = resource;
    seen_mode = mode;
    work_memory[0] = load_result != 0 ? 1 : 0;
    return load_result;
}

void gate_rtl_prepare_picture_display(void)
{
    assert(work_memory[0] != 0);
    call_order[ncalled++] = 'P';
}

void gate_rtl_draw_picture_record(GateFarPtr work)
{
    called('D', work);
    assert(work_memory[0] != 0);
}

void gate_rtl_release_picture_record(GateFarPtr work)
{
    called('C', work);
    assert(work_memory[0] != 0);
    work_memory[0] = 0;
}

static void run_case(uint16_t outcome, uint16_t resource, uint16_t mode)
{
    uint16_t result;
    work_ptr = (GateFarPtr){0x1234, 0xfff7};
    load_result = outcome;
    ncalled = 0;
    work_memory = 0;
    memset(call_order, 0, sizeof call_order);
    result = gate_rtl_show_picture(resource, mode);
    assert(result == (outcome != 0 ? 1 : 0));
    assert(seen_resource == resource && seen_mode == mode);
    if (outcome) {
        assert(ncalled == 4 && memcmp(call_order, "LPDC", 4) == 0);
    } else {
        assert(ncalled == 1 && call_order[0] == 'L');
    }
}

int main(void)
{
    run_case(0, 0xffff, 0x8000);
    run_case(1, 0x1234, 0);
    run_case(0xabcd, 0, 3);
    puts("original RTL image entry ordering and 136-byte work record: OK");
    return 0;
}
