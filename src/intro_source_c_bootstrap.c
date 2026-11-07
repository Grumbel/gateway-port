/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "intro_source_c_bootstrap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The unmodified original GATE.EXE image provides this exact data region in
 * the captured DS image. The remaining initialized runtime/RTL state is not
 * known to be a direct copy of any disk file and is deliberately untouched.
 * All offsets here identify provenance; no game bytes are compiled in. */
enum {
    GATE_EXE_SIZE = 22968,
    GATE_EXE_STATIC_FILE_OFFSET = 0x4251,
    GATE_EXE_STATIC_RAM_OFFSET = 0x2dd9d,
    GATE_EXE_STATIC_BYTES = 5869,
    GATE_RTL_SIZE = 109193,
    GATE_RTL_FILE_OFFSET = 0xd7bf,
    GATE_RTL_RAM_OFFSET = 0x1b77c,
    GATE_RTL_STATIC_BYTES = 768
};

enum {
    GATE_INTRO_PALETTE_RAM_OFFSET = 0x2b77c,
    GATE_INTRO_PALETTE_BYTES = 768,
    GATE_INTRO_PALETTE_ENTRY = 5,
    GATE_PIC_ENTRY_BYTES = 12
};

static uint32_t read_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static FILE *open_original(const char *dos_directory, const char *filename)
{
    size_t length, filename_length;
    char *path;
    FILE *file;

    if (!dos_directory || !filename)
        return NULL;
    length = strlen(dos_directory);
    filename_length = strlen(filename);
    if (length > (size_t)-1 - filename_length - 2u)
        return NULL;
    path = malloc(length + filename_length + 2u);
    if (!path)
        return NULL;
    memcpy(path, dos_directory, length);
    path[length] = '/';
    memcpy(path + length + 1u, filename, filename_length + 1u);
    file = fopen(path, "rb");
    free(path);
    return file;
}

int gate_intro_restore_static_exe_data(GateIntroMemory *memory,
                                       const char *dos_directory)
{
    FILE *file;
    uint8_t mz[2];
    int ok = 0;

    if (!memory)
        return 0;
    file = open_original(dos_directory, "GATE.EXE");
    if (!file)
        return 0;

    if (fread(mz, 1, sizeof mz, file) == sizeof mz &&
        mz[0] == 'M' && mz[1] == 'Z' &&
        fseek(file, 0, SEEK_END) == 0 &&
        ftell(file) == GATE_EXE_SIZE &&
        fseek(file, GATE_EXE_STATIC_FILE_OFFSET, SEEK_SET) == 0 &&
        fread(memory->bytes + GATE_EXE_STATIC_RAM_OFFSET,
              1, GATE_EXE_STATIC_BYTES, file) == GATE_EXE_STATIC_BYTES)
        ok = 1;
    if (fclose(file) != 0)
        ok = 0;
    return ok;
}

int gate_intro_restore_static_rtl_data(GateIntroMemory *memory,
                                       const char *dos_directory)
{
    FILE *file;
    uint8_t raw[GATE_RTL_STATIC_BYTES];
    unsigned i;
    int ok = 0;

    if (!memory)
        return 0;
    file = open_original(dos_directory, "GATE.RTL");
    if (!file)
        return 0;
    if (fseek(file, 0, SEEK_END) == 0 &&
        ftell(file) == GATE_RTL_SIZE &&
        fseek(file, GATE_RTL_FILE_OFFSET, SEEK_SET) == 0 &&
        fread(raw, 1, sizeof raw, file) == sizeof raw)
        ok = 1;
    if (fclose(file) != 0)
        ok = 0;
    if (!ok)
        return 0;

    /* In the pinned original, 722 of these 768 bytes are identical in the
     * resident image. The other 46 bytes comprise 23 little-endian RTLink
     * operand fixups, whose raw file words are both zero. Copying only
     * nonzero source bytes preserves all 23 relocated target values, which
     * still need to be supplied by the remaining startup state. */
    for (i = 0; i < sizeof raw; ++i)
        if (raw[i] != 0)
            memory->bytes[GATE_RTL_RAM_OFFSET + i] = raw[i];
    return 1;
}

int gate_intro_restore_initial_palette(GateIntroMemory *memory,
                                       const char *dos_directory)
{
    FILE *file;
    uint8_t entry[GATE_PIC_ENTRY_BYTES];
    uint8_t palette[GATE_INTRO_PALETTE_BYTES];
    long file_length;
    uint32_t picture_offset, palette_offset;
    int ok = 0;

    if (!memory)
        return 0;
    file = open_original(dos_directory, "GATE_301.PIC");
    if (!file)
        return 0;
    if (fseek(file, 0, SEEK_END) != 0)
        goto done;
    file_length = ftell(file);
    if (file_length < 0 ||
        fseek(file, GATE_PIC_ENTRY_BYTES * GATE_INTRO_PALETTE_ENTRY,
              SEEK_SET) != 0 ||
        fread(entry, 1, sizeof entry, file) != sizeof entry)
        goto done;

    picture_offset = read_u32_le(entry);
    palette_offset = picture_offset + 4u * entry[4];
    /* Gateway's PIC directory: entry offset, frame count, class/flags,
     * width, height; an optional 768-byte palette follows frame offsets.
     * This is the verified VGA 8-bit class-3 entry for 0x3105. */
    if ((entry[5] & 0x1fu) != 0x13u ||
        picture_offset < GATE_PIC_ENTRY_BYTES * (GATE_INTRO_PALETTE_ENTRY + 1) ||
        palette_offset < picture_offset ||
        (uint64_t)palette_offset + sizeof palette > (uint64_t)file_length ||
        fseek(file, (long)palette_offset, SEEK_SET) != 0 ||
        fread(palette, 1, sizeof palette, file) != sizeof palette)
        goto done;

    memcpy(memory->bytes + GATE_INTRO_PALETTE_RAM_OFFSET,
           palette, sizeof palette);
    ok = 1;
done:
    if (fclose(file) != 0)
        ok = 0;
    return ok;
}
