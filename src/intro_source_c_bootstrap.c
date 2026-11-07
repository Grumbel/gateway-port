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
    GATE_EXE_STATIC_BYTES = 5869
};

int gate_intro_restore_static_exe_data(GateIntroMemory *memory,
                                       const char *dos_directory)
{
    const char suffix[] = "/GATE.EXE";
    size_t length;
    char *path;
    FILE *file;
    uint8_t mz[2];
    int ok = 0;

    if (!memory || !dos_directory)
        return 0;
    length = strlen(dos_directory);
    if (length > (size_t)-1 - sizeof suffix)
        return 0;
    path = malloc(length + sizeof suffix);
    if (!path)
        return 0;
    memcpy(path, dos_directory, length);
    memcpy(path + length, suffix, sizeof suffix);
    file = fopen(path, "rb");
    free(path);
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
