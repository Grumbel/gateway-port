/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifdef NDEBUG
#undef NDEBUG
#endif

#include "gate_rtl.h"
#include "gate_dos_host_files.h"
#include "gate_intro_host_memory.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A successful one-byte read initializes both bytes of the local WORD as
 * needed by the source implementation. The original failed-read high byte
 * remains unmodeled, but this test requires only full successful reads. */
uint16_t gate_rtl_file_stack_word(void) { return 0; }
_Noreturn void gate_rtl_file_divide_fault(void) { abort(); }

static void test_original_resource(const char *snapshot, const char *directory)
{
    GateIntroMemory *memory = calloc(1, sizeof *memory);
    GateDosHostFiles files = {0};
    GateFarPtr font, width, spacing, bitmap;
    FILE *capture, *source;
    uint8_t bytes[2048];
    char name[1024];
    size_t size;
    unsigned i;

    assert(memory);
    capture = fopen(snapshot, "rb");
    assert(capture);
    assert(fread(memory->bytes, 1, GATE_INTRO_RAM_BYTES, capture) ==
           GATE_INTRO_RAM_BYTES);
    assert(fclose(capture) == 0);
    memory->ds_segment = 0x2a7a; /* original 22-second GATE.EXE DS */
    gate_intro_memory_attach(memory);
    files.directory = directory;
    gate_dos_host_files_attach(&files);

    /* Start from the captured program's actual linked file tables, mode
     * descriptors and font slot addresses, but force a clean cache miss.
     * The original font/resource arrays remain external DOS assets. */
    assert(gate_ds_word(0x4d84) == 0x2a7a);
    assert(gate_far_word(gate_ds_word(0x4d84), 0x0fce) == 3);
    assert(gate_ds_word(0x4d86) == 0x2a70);
    assert(gate_ds_word(0x54ca) == 0x54ec);
    for (i = 0; i < 2; ++i) {
        uint16_t at = (uint16_t)(0x54ce + i * 30u);
        gate_ds_set_word(at, 0);
        gate_far_set_byte(memory->ds_segment, (uint16_t)(at + 2), 0xff);
        gate_far_set_byte(memory->ds_segment, (uint16_t)(at + 3), 0xff);
    }

    font = gate_rtl_e873(5); /* original resource type 3, ID 305 */
    assert(font.segment == 0x2a7a && font.offset == 0x54ce);
    assert(gate_ds_word(0x54ca) == font.offset);
    assert(gate_far_byte(font.segment, font.offset + 3) == 5);
    assert(gate_far_byte(font.segment, font.offset + 11) == 0xff);
    assert(gate_far_byte(font.segment, font.offset + 12) == 0xff);
    assert(gate_dos_host_file_stream(5) == 0); /* original close executed */

    assert(snprintf(name, sizeof name, "%s/GATE_305.FNT", directory) > 0);
    source = fopen(name, "rb");
    assert(source);
    size = fread(bytes, 1, sizeof bytes, source);
    assert(!ferror(source));
    assert(fclose(source) == 0);
    assert(size == 1216);
    for (i = 0; i < 10; ++i)
        assert(gate_far_byte(font.segment, (uint16_t)(font.offset + 4 + i)) ==
               bytes[i]);

    width = (GateFarPtr){gate_far_word(font.segment, font.offset + 16),
                         gate_far_word(font.segment, font.offset + 14)};
    spacing = (GateFarPtr){gate_far_word(font.segment, font.offset + 20),
                           gate_far_word(font.segment, font.offset + 18)};
    bitmap = (GateFarPtr){gate_far_word(font.segment, font.offset + 28),
                          gate_far_word(font.segment, font.offset + 26)};
    assert(width.segment != 0 && spacing.segment != 0 && bitmap.segment != 0);
    for (i = 0; i < 128; ++i) {
        assert(gate_far_byte(width.segment, (uint16_t)(width.offset + i)) ==
               bytes[10 + i]);
        assert(gate_far_byte(spacing.segment, (uint16_t)(spacing.offset + i)) ==
               bytes[138 + i]);
    }
    for (i = 0; i < size - 266; ++i)
        assert(gate_far_byte(bitmap.segment, (uint16_t)(bitmap.offset + i)) ==
               bytes[266 + i]);

    /* The complete original font-width and height functions operate on
     * the freshly loaded native FAR arrays, not the saved guest arrays. */
    assert(gate_rtl_ebf7() == 11);
    assert(gate_rtl_ec0a('M') == 9);
    assert(gate_far_word(0x2a70, 0x32) == 9);
    assert(gate_far_word(0x2a70, 0x34) == 11);
    assert(gate_far_word(0x2a70, 0x36) == 4);
    assert(gate_far_word(0x2a70, 0x38) == 5);

    gate_rtl_e771();
    assert(gate_ds_word(0x54ce) == 0);
    assert(gate_ds_word(0x54ec) == 0);
    gate_dos_host_files_close_all(&files);
    gate_intro_memory_detach();
    free(memory);
    puts("GATE_305.FNT original header/width/spacing/bitmap + native metrics: OK");
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        puts("usage: launcher-intro-native-font-tests snapshot.bin DOS-directory");
        return 2;
    }
    test_original_resource(argv[1], argv[2]);
    return 0;
}
