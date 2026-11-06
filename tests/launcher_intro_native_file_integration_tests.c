/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "gate_rtl_file.h"
#include "gate_rtl_image.h"
#include "gate_dos_host_files.h"
#include "gate_intro_host_memory.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Two deliberate unresolved RTL edges. Successful 2-byte file reads replace
 * the entire stack WORD, so its original uninitialized value is not
 * observable on the tested selector path. Division by zero still traps. */
uint16_t gate_rtl_file_stack_word(void) { return 0; }
_Noreturn void gate_rtl_file_divide_fault(void) { abort(); }

static void guest_string(GateFarPtr pointer, const char *text)
{
    size_t i;
    for (i = 0; i <= strlen(text); ++i)
        gate_far_set_byte(pointer.segment, (uint16_t)(pointer.offset + i),
                          (uint8_t)text[i]);
}

static void test_original_rtl_file_opener_using_native_memory_and_files(void)
{
    char directory[] = "/tmp/gateway-intro-native-XXXXXX";
    char filename[256];
    GateIntroMemory *memory = calloc(1, sizeof *memory);
    GateDosHostFiles files = {0};
    FILE *asset;
    uint16_t handle;
    const uint16_t ds = 0x2300;
    const uint16_t suffix_offset = 0x3000;
    GateFarPtr payload = {0x4000, 0x0200};

    assert(memory);
    assert(mkdtemp(directory));
    files.directory = directory;
    assert(snprintf(filename, sizeof filename, "%s/GATE_201.PIC", directory) > 0);
    asset = fopen(filename, "wb");
    assert(asset);
    assert(fwrite("PIC!", 1, 4, asset) == 4);
    assert(fclose(asset) == 0);

    memory->ds_segment = ds;
    gate_intro_memory_attach(memory);
    gate_dos_host_files_attach(&files);
    guest_string((GateFarPtr){ds, 0x0faa}, "%s_%03d.%s");
    guest_string((GateFarPtr){ds, 0x2000}, "GATE");
    guest_string((GateFarPtr){ds, suffix_offset}, "PIC");
    /* Real GATE.RTL:e4ff initializes the base FAR pointer. Its e531
     * argument table is read from DS:0fba, not hardcoded in the adapter. */
    gate_rtl_e4ff((GateFarPtr){ds, 0x2000});
    gate_ds_set_word(0x0fba, suffix_offset);
    gate_ds_set_word(0x0fbc, ds);

    handle = gate_rtl_e531(1, 201);
    assert(handle >= 5);
    assert(gate_rtl_e5e3(handle, payload, 4) == 4);
    assert(gate_far_byte(payload.segment, payload.offset) == 'P');
    assert(gate_far_byte(payload.segment, payload.offset + 1) == 'I');
    assert(gate_far_byte(payload.segment, payload.offset + 2) == 'C');
    assert(gate_far_byte(payload.segment, payload.offset + 3) == '!');
    gate_rtl_e68b(handle);
    assert(gate_rtl_e531(1, 202) == 0); /* missing asset */

    gate_dos_host_files_close_all(&files);
    gate_intro_memory_detach();
    free(memory);
    assert(remove(filename) == 0);
    assert(rmdir(directory) == 0);
}

/* Optional diagnostic for developers who possess the original DOS game.
 * Verifies the original linked DS table and resource name against an actual
 * guest RAM capture. Neither the RAM image nor proprietary PIC is shipped. */
static void test_original_linked_snapshot(const char *capture,
                                         const char *game_directory)
{
    char filename[1024];
    uint8_t actual[16];
    uint8_t expected[16];
    GateIntroMemory *memory = calloc(1, sizeof *memory);
    GateDosHostFiles files = {0};
    FILE *snapshot, *asset;
    uint16_t handle;
    assert(memory);
    snapshot = fopen(capture, "rb");
    assert(snapshot);
    assert(fread(memory->bytes, 1, GATE_INTRO_RAM_BYTES, snapshot) ==
           GATE_INTRO_RAM_BYTES);
    assert(fclose(snapshot) == 0);
    memory->ds_segment = 0x2a7a; /* recorded DS in at-22000.regs */
    gate_intro_memory_attach(memory);
    files.directory = game_directory;
    gate_dos_host_files_attach(&files);

    assert(gate_ds_word(0x54c2) == 0x3592);
    assert(gate_ds_word(0x54c4) == memory->ds_segment);
    assert(gate_rtl_e531(0, 200) >= 5);
    handle = 5; /* first DOS file handle in a fresh host */
    assert(gate_rtl_e5e3(handle, (GateFarPtr){0x4000, 0x200}, 16) == 16);
    for (unsigned i = 0; i < sizeof actual; ++i)
        actual[i] = gate_far_byte(0x4000, (uint16_t)(0x200 + i));
    assert(snprintf(filename, sizeof filename, "%s/GATE_200.PIC",
                    game_directory) > 0);
    asset = fopen(filename, "rb");
    assert(asset);
    assert(fread(expected, 1, sizeof expected, asset) == sizeof expected);
    assert(fclose(asset) == 0);
    assert(memcmp(actual, expected, sizeof actual) == 0);
    gate_rtl_e68b(handle);

    /* The reconstructed resource-selector C follows original 0c33 aliases
     * into the same native file and segmented-memory adapters. */
    gate_rtl_resource_ds_set_word(8, 0xffffu); /* no live guest FD imported */
    assert(gate_rtl_11ecb(0x3001, 1) == 1);
    assert(gate_rtl_resource_ds_word(8) == 300);
    assert(gate_far_word(gate_rtl_image_header_segment(),
                         GATE_RTL_HEADER_OFFSET + 6) == 0x010e);
    assert(gate_far_word(gate_rtl_image_header_segment(),
                         GATE_RTL_HEADER_OFFSET + 8) == 0x0026);
    assert(gate_far_byte(gate_rtl_image_header_segment(),
                         GATE_RTL_HEADER_OFFSET + 5) == 0x43);
    puts("reconstructed PIC selector read original GATE_300.PIC entry #1: OK");
    gate_dos_host_files_close_all(&files);
    gate_intro_memory_detach();
    free(memory);
    puts("actual captured linked RTL DS and GATE_200.PIC open/read: OK");
}

int main(int argc, char **argv)
{
    test_original_rtl_file_opener_using_native_memory_and_files();
    if (argc == 3)
        test_original_linked_snapshot(argv[1], argv[2]);
    puts("real reconstructed RTL e4ff/e531/e5e3/e68b with native DOS host: OK");
    return 0;
}
