/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Exercised in DOSBox by the original Microsoft C 5.1 DOS toolchain. */
#include "GATEDOS.H"
#include <stdio.h>
#include <string.h>

static int check(int condition, const char *description)
{
    if (!condition)
        printf("DOS file probe: FAIL %s\n", description);
    return condition;
}

int main(void)
{
    char filename[10];
    unsigned char source[4];
    unsigned char destination[4];
    GateFarPtr name_ptr;
    GateFarPtr src_ptr;
    GateFarPtr dst_ptr;
    GateDosFileResult result;
    FILE *existing;
    uint16_t handle;
    int valid = 1;
    int opened = 0;
    int created = 0;

    /* The DOSBox D: mount is writable and only this named test file is used. */
    strcpy(filename, "GWTST.TMP");
    existing = fopen(filename, "rb");
    if (existing != NULL) {
        fclose(existing);
        puts("DOS file probe: GWTST.TMP already exists; refusing to overwrite it");
        return 1;
    }
    name_ptr = gate_stack_pointer(filename);
    src_ptr = gate_stack_pointer(source);
    dst_ptr = gate_stack_pointer(destination);
    source[0] = 0x00u;
    source[1] = 0x12u;
    source[2] = 0xffu;
    source[3] = 0x7fu;

    result = gate_dos_file_open(name_ptr, 2u, 1u);
    if (!check(!result.carry, "create")) {
        valid = 0;
        goto cleanup;
    }
    handle = result.ax;
    opened = 1;
    created = 1;
    result = gate_dos_file_write(handle, src_ptr, 4u);
    valid &= check(!result.carry && result.ax == 4u, "write 4 bytes");
    result = gate_dos_file_seek(handle, 0u, 0u);
    valid &= check(!result.carry && result.ax == 0u && result.dx == 0u,
                   "seek to start");
    result = gate_dos_file_close(handle);
    valid &= check(!result.carry, "close created file");
    opened = 0;

    result = gate_dos_file_open(name_ptr, 0u, 0u);
    if (!check(!result.carry, "reopen read-only")) {
        valid = 0;
        goto cleanup;
    }
    handle = result.ax;
    opened = 1;
    destination[0] = 0;
    destination[1] = 0;
    destination[2] = 0;
    destination[3] = 0;
    result = gate_dos_file_read(handle, dst_ptr, 4u);
    valid &= check(!result.carry && result.ax == 4u, "read 4 bytes");
    valid &= check(destination[0] == source[0] && destination[1] == source[1] &&
                   destination[2] == source[2] && destination[3] == source[3],
                   "read bytes match");
    result = gate_dos_file_read(handle, dst_ptr, 4u);
    valid &= check(!result.carry && result.ax == 0u, "EOF returns zero");
    result = gate_dos_file_seek(handle, 0u, 2u);
    valid &= check(!result.carry && result.ax == 2u && result.dx == 0u,
                   "seek position DX:AX");
    result = gate_dos_file_read(handle, dst_ptr, 1u);
    valid &= check(!result.carry && result.ax == 1u && destination[0] == 0xffu,
                   "read after seek");
    result = gate_dos_file_close(handle);
    valid &= check(!result.carry, "close read-only file");
    opened = 0;

    /* Invalid handle verifies DOS CF independently of AX's contents. */
    result = gate_dos_file_read(0xffffu, dst_ptr, 1u);
    valid &= check(result.carry != 0, "invalid handle sets carry");

cleanup:
    if (opened)
        (void)gate_dos_file_close(handle);
    if (created && remove(filename) != 0)
        valid &= check(0, "remove test file");
    puts(valid ? "DOS file probe: PASS" : "DOS file probe: FAIL");
    return valid ? 0 : 1;
}
