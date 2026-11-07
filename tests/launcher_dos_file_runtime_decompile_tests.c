/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gate_rtl_file.h"
#include "gate_dos_file_runtime.h"

#include <assert.h>
#include <stdio.h>

static const GateFarPtr path = {0x2222, 0x3333};
static const GateFarPtr buffer = {0x4444, 0x5555};
static GateDosFileResult result;
static uint16_t requested_mode, requested_create, requested_handle;
static uint16_t requested_count, requested_origin;
static uint32_t requested_offset;
static GateFarPtr requested_ptr;
static unsigned call_count;

static GateDosFileResult forward(GateFarPtr pointer, uint16_t handle,
                                  uint16_t count, uint16_t origin,
                                  uint32_t offset)
{
    requested_ptr = pointer;
    requested_handle = handle;
    requested_count = count;
    requested_origin = origin;
    requested_offset = offset;
    ++call_count;
    return result;
}

GateDosFileResult gate_dos_file_open(GateFarPtr pointer, uint16_t access,
                                     uint16_t create)
{
    requested_mode = access;
    requested_create = create;
    return forward(pointer, 0, 0, 0, 0);
}

GateDosFileResult gate_dos_file_close(uint16_t handle)
{
    return forward((GateFarPtr){0, 0}, handle, 0, 0, 0);
}

GateDosFileResult gate_dos_file_read(uint16_t handle, GateFarPtr pointer,
                                     uint16_t count)
{
    return forward(pointer, handle, count, 0, 0);
}

GateDosFileResult gate_dos_file_write(uint16_t handle, GateFarPtr pointer,
                                      uint16_t count)
{
    return forward(pointer, handle, count, 0, 0);
}

GateDosFileResult gate_dos_file_seek(uint16_t handle, uint16_t origin,
                                     uint32_t offset)
{
    return forward((GateFarPtr){0, 0}, handle, 0, origin, offset);
}

static void test_open_and_create(void)
{
    call_count = 0;
    result = (GateDosFileResult){0x2345, 0x4567, 0};
    assert(gate_linked_dos_file_open(path, 0x1202) == 0x2345);
    assert(requested_ptr.segment == path.segment &&
           requested_ptr.offset == path.offset);
    assert(requested_mode == 2 && requested_create == 0);
    assert(gate_linked_dos_file_open(path, 0x0180) == 0x2345);
    assert(requested_mode == 0x80 && requested_create == 1);
    result.carry = 1;
    assert(gate_linked_dos_file_open(path, 0) == 0);
    assert(call_count == 3);
}

static void test_close_read_write_seek(void)
{
    call_count = 0;
    result = (GateDosFileResult){0x0801, 0x789a, 0};
    assert(gate_linked_dos_file_close(7) == 0x0801);
    assert(requested_handle == 7);
    result.carry = 1;
    assert(gate_linked_dos_file_close(9) == 0x0801);
    assert(requested_handle == 9);

    result.carry = 0;
    assert(gate_linked_dos_file_read(5, buffer, 0xffff) == 0x0801);
    assert(requested_ptr.segment == buffer.segment &&
           requested_ptr.offset == buffer.offset);
    assert(requested_handle == 5 && requested_count == 0xffff);
    result.carry = 1;
    assert(gate_linked_dos_file_read(5, buffer, 3) == 0);

    result.carry = 0;
    assert(gate_linked_dos_file_write(6, buffer, 15) == 0x0801);
    assert(requested_count == 15 && requested_handle == 6);
    result.carry = 1;
    assert(gate_linked_dos_file_write(6, buffer, 15) == 0);

    result.carry = 0;
    assert(gate_linked_dos_file_seek(4, 0x0102, 0xff123456u) == 0);
    assert(requested_handle == 4 && requested_origin == 2 &&
           requested_offset == 0xff123456u);
    result.carry = 1;
    assert(gate_linked_dos_file_seek(4, 0, 0xffffffffu) == 0xffff);
    assert(call_count == 8);
}

int main(void)
{
    test_open_and_create();
    test_close_read_write_seek();
    puts("original 1c29 DOS file wrappers, carry and argument ABI: OK");
    return 0;
}
