/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Mock DOS resource ownership, while the independent data-host fixture
 * verifies every relocated byte of both images against the originals. */
#include "BOOT.H"
#include <stdio.h>
#include <stdlib.h>

static int fail_at, allocations, releases, open_files, opens, closes;
static int alloc_attempts, resident_calls, data_calls, resident_crcs, data_crcs;
static uint16_t current_ds;

static void require(int ok)
{
    if (!ok) {
        fputs("DOS intro owner test failed\n", stderr);
        abort();
    }
}

unsigned int _dos_allocmem(unsigned int paragraphs, unsigned int *segment)
{
    ++alloc_attempts;
    require(paragraphs == (alloc_attempts == 1 ? 0x100au : 0x03d0u));
    if (fail_at == alloc_attempts)
        return 8u;
    *segment = alloc_attempts == 1 ? 0x2300u : 0x4000u;
    ++allocations;
    return 0u;
}

unsigned int _dos_freemem(unsigned int segment)
{
    require(segment == 0x2300u || segment == 0x4000u);
    require(allocations > 0);
    --allocations;
    ++releases;
    return 0u;
}

void gate_dos_bind_ds(uint16_t segment)
{
    current_ds = segment;
}

GateDosFileResult gate_dos_file_open(GateFarPtr path, uint16_t access,
                                     uint16_t create)
{
    GateDosFileResult result;
    ++opens;
    require(path.segment == (opens == 1 ? 0x1111u : 0x2222u));
    require(access == 0u && create == 0u);
    result.carry = (uint8_t)(fail_at == (opens == 1 ? 3 : 4));
    result.ax = opens == 1 ? 7u : 8u;
    result.dx = 0u;
    if (!result.carry)
        ++open_files;
    return result;
}

GateDosFileResult gate_dos_file_close(uint16_t handle)
{
    GateDosFileResult result;
    ++closes;
    require(handle == (open_files == 2 ? 8u : 7u));
    require(open_files > 0);
    --open_files;
    result.carry = (uint8_t)(fail_at == (handle == 8u ? 9 : 10));
    result.ax = result.dx = 0u;
    return result;
}

int gate_dos_prepare_intro_resident_bank_with_exe(uint16_t rtl_handle,
       uint16_t exe_handle, uint16_t resident_segment,
       uint16_t paragraphs, uint16_t data_segment, GateFarPtr exe_callback)
{
    ++resident_calls;
    require(rtl_handle == 8u && exe_handle == 7u);
    require(resident_segment == 0x4000u && paragraphs == 0x03d0u);
    require(data_segment == 0x230au);
    require(exe_callback.segment == 0x7777u && exe_callback.offset == 0x1234u);
    return fail_at == 5 ? -2 : 0;
}

uint32_t gate_dos_intro_resident_crc32_with_callback(uint16_t segment,
                                uint16_t paragraphs, uint16_t callback_offset)
{
    ++resident_crcs;
    require(segment == 0x4000u && paragraphs == 0x03d0u);
    require(callback_offset == 0x1234u);
    return fail_at == 6 ? 0u : 0xf332bf93ul;
}

int gate_dos_prepare_intro_data(uint16_t exe_handle, uint16_t rtl_handle,
       uint16_t lower_segment, uint16_t paragraphs,
       uint16_t resident_segment, uint16_t external_a_segment,
       uint16_t external_b_segment, GateFarPtr return_callback)
{
    ++data_calls;
    require(exe_handle == 7u && rtl_handle == 8u);
    require(lower_segment == 0x2300u && paragraphs == 0x100au);
    require(resident_segment == 0x4000u);
    require(external_a_segment == 0x5555u && external_b_segment == 0x6666u);
    require(return_callback.segment == 0x8888u && return_callback.offset == 0x5678u);
    gate_dos_bind_ds(0x230au);
    return fail_at == 7 ? -4 : 0;
}

uint32_t gate_dos_intro_data_crc32(uint16_t segment, uint16_t paragraphs)
{
    ++data_crcs;
    require(segment == 0x2300u && paragraphs == 0x100au);
    return fail_at == 8 ? 0u : 0x5a991491ul;
}

static GateDosBootConfig config(void)
{
    GateDosBootConfig c;
    c.exe_path.segment = 0x1111u; c.exe_path.offset = 0u;
    c.rtl_path.segment = 0x2222u; c.rtl_path.offset = 0u;
    c.exe_callback.segment = 0x7777u; c.exe_callback.offset = 0x1234u;
    c.return_callback.segment = 0x8888u; c.return_callback.offset = 0x5678u;
    c.external_a_segment = 0x5555u;
    c.external_b_segment = 0x6666u;
    return c;
}

static void reset(int fault)
{
    fail_at = fault;
    allocations = releases = open_files = opens = closes = 0;
    alloc_attempts = resident_calls = data_calls = resident_crcs = data_crcs = 0;
    current_ds = 0u;
}

int main(void)
{
    GateDosBootConfig c;
    GateDosBootImage image;
    int stage, result;

    for (stage = 0; stage <= 10; ++stage) {
        reset(stage);
        c = config();
        image.lower_segment = image.resident_segment = 0u;
        result = gate_dos_boot_prepare(&c, &image);
        if (stage == 0) {
            require(result == 0);
            require(image.lower_segment == 0x2300u && image.resident_segment == 0x4000u);
            require(current_ds == 0x230au);
            require(gate_dos_boot_prepare(&c, &image) == -1);
            require(alloc_attempts == 2);
            gate_dos_boot_release(&image);
        } else {
            require(result == -(stage == 1 ? 2 : stage == 2 ? 3 :
                                stage == 3 ? 4 : stage == 4 ? 5 :
                                stage == 5 ? 6 : stage == 6 ? 9 :
                                stage == 7 ? 7 : stage == 8 ? 10 : 8));
        }
        require(allocations == 0 && open_files == 0);
        require(image.lower_segment == 0u && image.resident_segment == 0u);
        require(current_ds == 0u);
        require(releases == (stage == 1 ? 0 : stage == 2 ? 1 : 2));
    }
    reset(0);
    c = config();
    c.exe_callback.segment = 0u;
    image.lower_segment = image.resident_segment = 0u;
    require(gate_dos_boot_prepare(&c, &image) == -1);
    require(alloc_attempts == 0 && opens == 0);
    puts("DOS intro owner: 11 allocation/file/CRC outcomes and invalid callback PASS");
    return 0;
}
