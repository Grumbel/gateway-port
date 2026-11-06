/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "gate_rtl_file.h"
#include "gate_dos_host_files.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint8_t ram[0x10000];
static const uint16_t segment = 0x4444;

uint8_t gate_far_byte(uint16_t address_segment, uint16_t offset)
{
    assert(address_segment == segment);
    return ram[offset];
}

void gate_far_set_byte(uint16_t address_segment, uint16_t offset, uint8_t value)
{
    assert(address_segment == segment);
    ram[offset] = value;
}

static void test_native_dos_file_backend(void)
{
    char directory[] = "/tmp/gateway-dos-files-XXXXXX";
    char filename[200];
    GateDosHostFiles files = {0};
    const GateFarPtr guest_name = {0x4444, 0x0100};
    const GateFarPtr source = {0x4444, 0xfffe};
    const GateFarPtr destination = {0x4444, 0x0300};
    uint16_t handle;

    assert(mkdtemp(directory) != 0);
    files.directory = directory;
    assert(snprintf(filename, sizeof filename, "%s/GATE_201.PIC", directory) > 0);
    memcpy(ram + 0x0100, "C:\\GATE_201.PIC", sizeof "C:\\GATE_201.PIC");
    ram[0xfffe] = 'A'; ram[0xffff] = 'B'; ram[0] = 'C'; ram[1] = 'D';

    gate_dos_host_files_attach(&files);
    handle = gate_far_1c29_0000(guest_name, 0x80);
    assert(handle >= 5);
    assert(gate_far_1c29_0055(handle, source, 4) == 4);
    assert(gate_far_1c29_0074(handle, 0, 0) == 0);
    assert(gate_far_1c29_0036(handle, destination, 4) == 4);
    assert(memcmp(ram + 0x0300, "ABCD", 4) == 0);
    assert(gate_far_1c29_0036(handle, destination, 4) == 0);
    assert(gate_far_1c29_0074(handle, 2, 0xffffffffu) == 0);
    assert(gate_far_1c29_0036(handle, destination, 1) == 1);
    assert(ram[0x0300] == 'D');
    assert(gate_far_1c29_0028(handle) == 0);

    /* Original runtime format used by rtl:e531, with signed 16-bit %03d. */
    memcpy(ram + 0x0500, "%s_%03d.%s", sizeof "%s_%03d.%s");
    memcpy(ram + 0x0520, "GATE", sizeof "GATE");
    memcpy(ram + 0x0540, "PIC", sizeof "PIC");
    gate_far_12a5_3366((GateFarPtr){segment, 0x0600},
        (GateFarPtr){segment, 0x0500}, (GateFarPtr){segment, 0x0520},
        201, (GateFarPtr){segment, 0x0540});
    assert(strcmp((char *)(ram + 0x0600), "GATE_201.PIC") == 0);
    assert(gate_far_12a5_3b52((GateFarPtr){segment, 0x0600}, 4) == 0);
    assert(gate_far_12a5_3b52((GateFarPtr){segment, 0x0600}, 5) == 0xffff);

    handle = gate_far_1c29_0000(guest_name, 0);
    assert(handle >= 5);
    assert(gate_far_1c29_0036(handle, destination, 4) == 4);
    assert(gate_far_1c29_0028(handle) == 0);
    assert(gate_far_1c29_0036(handle, destination, 1) == 0);
    assert(gate_far_1c29_0074(handle, 0, 0) == 0xffff);

    memcpy(ram + 0x0100, "C:\\..", sizeof "C:\\..");
    assert(gate_far_1c29_0000(guest_name, 0x80) == 0);
    gate_dos_host_files_close_all(&files);

    assert(remove(filename) == 0);
    assert(rmdir(directory) == 0);
}

int main(void)
{
    test_native_dos_file_backend();
    puts("native DOS asset file backend, create/read/seek/close and FAR wrap: OK");
    return 0;
}
