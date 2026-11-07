/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gate_exe.h"
#include "gate_intro_host_memory.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static uint16_t original_return;
static unsigned calls;

uint16_t gate_far_0e2c_0094(uint16_t argument)
{
    assert(argument == 0x1000);
    ++calls;
    return original_return;
}

static void read_ram(const char *path, uint8_t *target)
{
    FILE *file = fopen(path, "rb");
    assert(file != NULL);
    assert(fread(target, 1, GATE_INTRO_RAM_BYTES, file) == GATE_INTRO_RAM_BYTES);
    assert(fgetc(file) == EOF);
    assert(fclose(file) == 0);
}

int main(int argc, char **argv)
{
    static const struct { uint16_t selector, offset; } globals[] = {
        {0x4e46, 0x0660}, {0x4e48, 0x065c}, {0x4e4a, 0x065e},
        {0x4e4c, 0x064e}, {0x4e4e, 0x0658},
    };
    GateIntroMemory *before;
    GateIntroMemory *after;
    unsigned i;

    if (argc != 3) {
        fprintf(stderr, "usage: %s INTRO-ENTRY.bin INTRO-SETUP-END.bin\n", argv[0]);
        return 2;
    }
    before = calloc(1, sizeof *before);
    after = calloc(1, sizeof *after);
    assert(before != NULL && after != NULL);
    read_ram(argv[1], before->bytes);
    read_ram(argv[2], after->bytes);
    before->ds_segment = 0x2a7a; /* from original entry .regs */

    gate_intro_memory_attach(before);
    /* Original word at DS:4e4e destination was 0x524e at the 134c
     * breakpoint, following the original 0e2c:0094 result 0x6500. */
    original_return = 0x6500;
    gate_exe_1302_setup();
    assert(calls == 1);
    for (i = 0; i < sizeof globals / sizeof globals[0]; ++i) {
        uint16_t segment = gate_ds_word(globals[i].selector);
        uint16_t expected = (uint16_t)(after->bytes[
            ((uint32_t)segment << 4) + globals[i].offset] |
            ((uint16_t)after->bytes[((uint32_t)segment << 4) +
                                    globals[i].offset + 1] << 8));
        assert(gate_far_word(segment, globals[i].offset) == expected);
    }
    assert(gate_far_word(0x2a7a, 0x0660) == 0x0401);
    assert(gate_far_word(0x2a7a, 0x0658) == 0x524e);
    gate_intro_memory_detach();
    free(after);
    free(before);
    puts("original GATE.EXE:1302 entry setup matches five guest words: OK");
    return 0;
}
