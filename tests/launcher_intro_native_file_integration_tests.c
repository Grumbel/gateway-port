/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "gate_rtl_file.h"
#include "gate_rtl_image.h"
#include "gate_rtl_graphics.h"
#include "gate_dos_host_files.h"
#include "gate_intro_host_memory.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Reference from the independently developed C++ PIC parser, tests only. */
int gate_intro_planar_pic_matches_reference(const char *directory,
                                            uint16_t index,
                                            uint16_t x, uint16_t y);

/* Two deliberate unresolved RTL edges. Successful 2-byte file reads replace
 * the entire stack WORD, so its original uninitialized value is not
 * observable on the tested selector path. Division by zero still traps. */
uint16_t gate_rtl_file_stack_word(void) { return 0; }
_Noreturn void gate_rtl_file_divide_fault(void) { abort(); }

/* The original 0eec:018a service is provided by the separate C++ DCL host
 * bridge, using the *already selected* DOS file cursor. The remaining
 * callbacks below are not image decoding: state save/restore is a VGA host
 * responsibility. Post-load correction uses the actual recovered RTL C.
 * This integration exercises the ordinary (no-transform) DCL path. */
uint16_t gate_far_1e48_04d9(void) { return 0; }
void gate_far_1e48_03c5(uint16_t saved) { (void)saved; }
uint16_t gate_far_12a5_3b0c(void) { return 0; }
void gate_far_223f_00b6(uint32_t milliseconds) { (void)milliseconds; }
GateFarPtr gate_far_0cdc_0430(GateFarPtr palette) { return palette; }
GateFarPtr gate_far_0cdc_0396(GateFarPtr palette) { return palette; }
void gate_far_1258_172e(GateFarPtr message) { (void)message; abort(); }
void gate_rtl_131cb(GateFarPtr work)
{
    if (gate_far_byte(work.segment, work.offset)) {
        gate_rtl_graphics_release((GateFarPtr){work.segment,
            (uint16_t)(work.offset + 8u)});
        gate_far_set_byte(work.segment, work.offset, 0);
    }
}

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
    assert(gate_rtl_resource_ds_word(0x0a) == 24);
    assert(gate_rtl_resource_ds_word(0x0c) == 81);
    assert(gate_rtl_120f3() == 0); /* no post-load format transform */
    /* Continue with reconstructed original 145d VFX allocation, its
     * 1479 fill and the 24a7 VGA rasterizer. Only the GATE_300 descriptor
     * is original picture data here; the image pixels are still a test
     * pattern, pending the original 0eec:018a resource decoder. */
    {
        const uint16_t header = gate_rtl_image_header_segment();
        GateFarPtr image = {0x4000, 0x1000};
        uint16_t pixels;
        assert(gate_far_word(0x2a7a, 0x15cc) == 3);
        assert(gate_far_145d_00f7(
            gate_far_word(header, GATE_RTL_HEADER_OFFSET + 8),
            gate_far_word(header, GATE_RTL_HEADER_OFFSET + 6),
            3, image, 0) == 0);
        assert(gate_far_word(image.segment, image.offset + 0x2c) == 270);
        assert(gate_far_word(image.segment, image.offset + 0x2e) == 38);
        pixels = gate_far_word(image.segment, image.offset + 0x1a);
        assert(pixels >= 0x6000);
        gate_far_1479_0002(0x44, image);
        gate_far_24a7_000c(0, 4, 5, 0, image);
        assert(gate_far_byte(0xa000, 320 * 4 + 5) == 0x44);
        assert(gate_far_byte(0xa000, 320 * 41 + 274) == 0x44);
        gate_rtl_graphics_release(image);
        assert(gate_far_word(image.segment, image.offset) == 0xffffu);
    }
    puts("reconstructed PIC selector -> VFX allocation -> native VGA blit: OK (test pixels)");

    /* Fully loaded original 270x38 PIC payload: reconstructed C selector
     * and allocator, native DCL host decoder at the original FAR service
     * boundary, reconstructed original C drawable record, and native VFX
     * draw into the VGA guest framebuffer. No proprietary data is copied
     * into the test or binary. */
    {
        GateFarPtr work = {0x4000, 0x1800};
        GateFarPtr image = {work.segment, (uint16_t)(work.offset + 8u)};
        uint16_t src_segment;
        uint16_t src_offset;
        unsigned nonzero = 0;
        gate_far_set_byte(work.segment, work.offset, 0);
        assert(gate_rtl_12c33(work, 0x3001, 1) == 1);
        assert(gate_far_word(work.segment, work.offset + 2) == 24);
        assert(gate_far_word(work.segment, work.offset + 4) == 81);
        assert(gate_far_word(image.segment, image.offset + 0x2c) == 270);
        assert(gate_far_word(image.segment, image.offset + 0x2e) == 38);
        src_offset = gate_far_word(image.segment, image.offset + 0x18);
        src_segment = gate_far_word(image.segment, image.offset + 0x1a);
        for (unsigned y = 0; y < 38; ++y)
            for (unsigned x = 0; x < 270; ++x)
                nonzero += gate_far_byte(src_segment,
                    (uint16_t)(src_offset + 270u * y + x)) != 0;
        assert(nonzero > 100);
        for (unsigned y = 0; y < 38; ++y)
            for (unsigned x = 0; x < 270; ++x)
                gate_far_set_byte(0xa000,
                    (uint16_t)(320u * (81u + y) + 24u + x), 0);
        gate_rtl_12ed7(work);
        for (unsigned y = 0; y < 38; ++y)
            for (unsigned x = 0; x < 270; ++x)
                assert(gate_far_byte(0xa000,
                           (uint16_t)(320u * (81u + y) + 24u + x)) ==
                       gate_far_byte(src_segment,
                           (uint16_t)(src_offset + 270u * y + x)));
        gate_rtl_131cb(work);
        assert(gate_far_byte(work.segment, work.offset) == 0);
        puts("original GATE_300.PIC #1 decoded and rendered through reconstructed C loader: OK");
    }
    gate_dos_host_files_close_all(&files);
    gate_intro_memory_detach();
    free(memory);
    puts("actual captured linked RTL DS and GATE_200.PIC open/read: OK");
}

/* Optional independent original mode-12h capture, taken before the intro
 * changes to 320x200 mode 13h. The 0x8101 selector resolves to resource #1
 * of GATE_201.PIC while the saved mode word is 2. Render the original
 * decoded 4-bit DCL stream through the reconstructed C loader, original
 * image constructor and native four-plane VGA rasterizer. */
static void test_original_planar_snapshot(const char *capture,
                                          const char *game_directory)
{
    GateIntroMemory *memory = calloc(1, sizeof *memory);
    GateDosHostFiles files = {0};
    FILE *snapshot;
    GateFarPtr work = {0x4000, 0x1800};
    GateFarPtr image = {work.segment, (uint16_t)(work.offset + 8u)};
    uint16_t width;
    uint16_t height;
    uint16_t x;
    uint16_t y;
    unsigned nonzero = 0;
    uint16_t row;
    uint16_t col;

    assert(memory);
    snapshot = fopen(capture, "rb");
    assert(snapshot);
    assert(fread(memory->bytes, 1, GATE_INTRO_RAM_BYTES, snapshot) ==
           GATE_INTRO_RAM_BYTES);
    assert(fclose(snapshot) == 0);
    memory->ds_segment = 0x2a7a;
    gate_intro_memory_attach(memory);
    files.directory = game_directory;
    gate_dos_host_files_attach(&files);
    assert(gate_ds_word(0x15cc) == 2);
    assert(gate_far_word(gate_rtl_image_mode_segment(),
                         GATE_RTL_VIDEO_MODE_OFFSET) == 2);
    gate_rtl_resource_ds_set_word(0, 0);
    gate_rtl_resource_ds_set_word(8, 0xffffu);
    gate_far_set_byte(work.segment, work.offset, 0);
    assert(gate_rtl_12c33(work, 0x8101, 1) == 1);
    assert(gate_rtl_resource_ds_word(8) == 201);
    assert(gate_far_word(image.segment, image.offset) == 0xca00);
    assert(gate_far_byte(image.segment, image.offset + 0x12) == 4);
    assert(gate_far_byte(image.segment, image.offset + 0x13) == 1);
    width = gate_far_word(image.segment, image.offset + 0x2c);
    height = gate_far_word(image.segment, image.offset + 0x2e);
    x = gate_far_word(work.segment, work.offset + 2);
    y = gate_far_word(work.segment, work.offset + 4);
    assert(width == 300 && height == 30);
    assert(x + width <= 640 && y + height <= 480);
    gate_rtl_12ed7(work);
    for (row = 0; row < height; ++row)
        for (col = 0; col < width; ++col)
            nonzero += gate_intro_vga16_pixel((uint16_t)(x + col),
                                                (uint16_t)(y + row)) != 0;
    assert(nonzero > 50);
    assert(gate_intro_planar_pic_matches_reference(game_directory, 1, x, y));
    if (x != 0)
        assert(gate_intro_vga16_pixel((uint16_t)(x - 1u), y) == 0);
    gate_rtl_131cb(work);
    gate_dos_host_files_close_all(&files);
    gate_intro_memory_detach();
    free(memory);
    puts("original GATE_201.PIC #1 decoded and rendered through native 4-plane VGA: OK");
}

int main(int argc, char **argv)
{
    test_original_rtl_file_opener_using_native_memory_and_files();
    if (argc == 4 && strcmp(argv[1], "--mode2") == 0)
        test_original_planar_snapshot(argv[2], argv[3]);
    else if (argc == 3)
        test_original_linked_snapshot(argv[1], argv[2]);
    puts("real reconstructed RTL e4ff/e531/e5e3/e68b with native DOS host: OK");
    return 0;
}
