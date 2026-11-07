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
    GATE_RTL_STATIC_BYTES = 768,
    GATE_RTL_RESIDENT_RAM_BASE = 0x2a777,
    GATE_RTL_RESIDENT_STATIC_BYTES = 140
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
    /* These seven source-C read windows were independently compared with
     * their on-disk RTL counterparts. Intermediate runtime globals and
     * pointers are deliberately NOT copied; several are mutable. */
    static const struct {
        uint32_t ram_offset;
        uint16_t bytes;
    } regions[] = {
        {0x2b730, 37}, /* Resource extensions and filename format. */
        {0x2ba9e, 68}, /* Mode/graphics size constants. */
        {0x2bbb8, 4},
        {0x2bbc4, 4},
        {0x2bc23, 7},
        {0x2d05e, 16}, /* Static image resource metadata. */
        {0x2b778, 4}
    };
    FILE *file;
    uint8_t raw[GATE_RTL_STATIC_BYTES];
    uint8_t resident[GATE_RTL_RESIDENT_STATIC_BYTES];
    unsigned i;
    size_t region_cursor = 0;
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
    if (ok) {
        for (i = 0; i < sizeof regions / sizeof regions[0]; ++i) {
            const uint32_t origin = regions[i].ram_offset -
                                    GATE_RTL_RESIDENT_RAM_BASE;
            if (region_cursor + regions[i].bytes > sizeof resident ||
                origin + regions[i].bytes > GATE_RTL_SIZE ||
                fseek(file, (long)origin, SEEK_SET) != 0 ||
                fread(resident + region_cursor, 1, regions[i].bytes, file) !=
                regions[i].bytes) {
                ok = 0;
                break;
            }
            region_cursor += regions[i].bytes;
        }
    }
    if (fclose(file) != 0)
        ok = 0;
    if (!ok)
        return 0;

    /* In the pinned original, 722 of these 768 bytes are identical in the
     * resident image. The other 46 bytes comprise 23 little-endian RTLink
     * operand fixups, whose raw file words are both zero. Copying only
     * nonzero source bytes preserves all 23 relocated target values, which
     * are not accessed by the corrected source-C palette path. */
    for (i = 0; i < sizeof raw; ++i)
        if (raw[i] != 0)
            memory->bytes[GATE_RTL_RAM_OFFSET + i] = raw[i];
    region_cursor = 0;
    for (i = 0; i < sizeof regions / sizeof regions[0]; ++i) {
        memcpy(memory->bytes + regions[i].ram_offset,
               resident + region_cursor, regions[i].bytes);
        region_cursor += regions[i].bytes;
    }
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

int gate_intro_restore_linked_selectors(GateIntroMemory *memory)
{
    /* The full original 22-second table at DS:4d80..4e50 consists of FAR
     * segment values DS or DS-10 paragraphs. These are the offsets whose
     * target is the DS bank. All other words target the adjacent bank.
     * The list describes the linked data layout, not executable opcodes. */
    static const uint16_t ds_target_offsets[] = {
        0x4d80, 0x4d84, 0x4db0, 0x4db8, 0x4dc0, 0x4dce,
        0x4dda, 0x4ddc, 0x4df2, 0x4df4, 0x4dfc, 0x4dfe,
        0x4e00, 0x4e12, 0x4e16, 0x4e1a, 0x4e1c, 0x4e22,
        0x4e24, 0x4e2a, 0x4e34, 0x4e36, 0x4e38, 0x4e3a,
        0x4e3c, 0x4e3e, 0x4e42, 0x4e46, 0x4e48, 0x4e4a,
        0x4e4c, 0x4e4e
    };
    const uint32_t base = memory ? (uint32_t)memory->ds_segment * 16u : 0;
    unsigned offset, index = 0;

    if (!memory || memory->ds_segment < 10 ||
        base + 0x4e52u > GATE_INTRO_RAM_BYTES)
        return 0;
    for (offset = 0x4d80; offset <= 0x4e50; offset += 2) {
        const uint16_t segment =
            (index < sizeof ds_target_offsets / sizeof ds_target_offsets[0] &&
             offset == ds_target_offsets[index]) ? memory->ds_segment :
            (uint16_t)(memory->ds_segment - 10u);
        if (segment == memory->ds_segment)
            ++index;
        memory->bytes[base + offset] = (uint8_t)segment;
        memory->bytes[base + offset + 1] = (uint8_t)(segment >> 8);
    }
    return index == sizeof ds_target_offsets / sizeof ds_target_offsets[0];
}

static void write_ds_word(GateIntroMemory *memory, uint16_t offset,
                          uint16_t value)
{
    const uint32_t physical = (uint32_t)memory->ds_segment * 16u + offset;
    memory->bytes[physical] = (uint8_t)value;
    memory->bytes[physical + 1] = (uint8_t)(value >> 8);
}

int gate_intro_restore_default_runtime_state(GateIntroMemory *memory)
{
    /* On this launch DS:0fb6/0fbe are FAR pointers into the resource-type
     * strings supplied by GATE.RTL's resident data, at file fbB and fc3.
     * The resident image is linked 0x29 bytes before the current DS base. */
    static const struct {
        uint16_t pointer_offset;
        uint16_t resident_file_offset;
    } resource_ext[] = {{0x0fb6, 0x0fbb}, {0x0fbe, 0x0fc3}};
    static const char executable_name[] = "GATE.EXE";
    const char *extension = strchr(executable_name, '.');
    const size_t stem_length = extension ? (size_t)(extension - executable_name) : 0;
    const uint32_t base = memory ? (uint32_t)memory->ds_segment * 16u : 0;
    unsigned i;

    if (!memory || base + 0x5518u > GATE_INTRO_RAM_BYTES ||
        stem_length > 0x40u || !stem_length)
        return 0;

    for (i = 0; i < sizeof resource_ext / sizeof resource_ext[0]; ++i) {
        write_ds_word(memory, resource_ext[i].pointer_offset,
                      (uint16_t)(resource_ext[i].resident_file_offset - 0x29u));
        write_ds_word(memory, (uint16_t)(resource_ext[i].pointer_offset + 2u),
                      memory->ds_segment);
    }

    /* Mode 3 (VGA 320x200, 256 colors) was already selected before the
     * pinned entry; RTL's active mode and renderer mode agree. */
    write_ds_word(memory, 0x0fce, 3);
    write_ds_word(memory, 0x15cc, 3);

    /* The original DOS filename constructor uses the executable stem as
     * its resource prefix, with a linked FAR pointer in DS:54c2. */
    memcpy(memory->bytes + base + 0x3592u, executable_name, stem_length);
    memory->bytes[base + 0x3592u + stem_length] = 0;
    write_ds_word(memory, 0x54c2, 0x3592);
    write_ds_word(memory, 0x54c4, memory->ds_segment);

    /* VGA standard color status port and the original VGA adapter type. */
    write_ds_word(memory, 0x5514, 0x03dau);
    write_ds_word(memory, 0x5516, 5);

    /* MIDIPlay's default AdLib-capable device byte is already installed at
     * this boundary. RTL:c3c5 checks bits 1/2 at EXE:134c. This copied
     * device *selection* is independent of any original IRQ8 driver code. */
    memory->bytes[base + 0x0662u] = 0x5a;
    return 1;
}

int gate_intro_restore_default_vectors(GateIntroMemory *memory)
{
    /* Original DOS/RTLink startup installed these seven interrupt vectors
     * before EXE:1302. Six targets are relative to the original EXE load
     * paragraph (DS - the original 1a7a data-segment relative paragraph),
     * and the BIOS mouse vector remains f000:0300. These are linked
     * addresses, not a copy of interrupt-handler executable instructions. */
    static const struct {
        uint8_t interrupt;
        uint16_t offset;
        uint16_t relative_segment;
    } linked[] = {
        {0x00, 0x0a58, 0x02a5}, /* DOS divide-error handler. */
        {0x08, 0x00ca, 0x072c}, /* Installed timer/music IRQ. */
        {0x1b, 0x000b, 0x0df1}, /* Ctrl-Break handler. */
        {0x1c, 0x08d0, 0x0f10}, /* Periodic clock callback. */
        {0x23, 0x000b, 0x0df1}, /* DOS Ctrl-C handler. */
        {0x24, 0x0008, 0x0df1}, /* DOS critical-error handler. */
    };
    const uint16_t original_exe_data_segment = 0x1a7a;
    uint16_t load_segment;
    unsigned i;

    if (!memory || memory->ds_segment < original_exe_data_segment)
        return 0;
    load_segment = (uint16_t)(memory->ds_segment - original_exe_data_segment);
    for (i = 0; i < sizeof linked / sizeof linked[0]; ++i) {
        const unsigned at = 4u * linked[i].interrupt;
        const uint16_t segment =
            (uint16_t)(load_segment + linked[i].relative_segment);
        memory->bytes[at] = (uint8_t)linked[i].offset;
        memory->bytes[at + 1] = (uint8_t)(linked[i].offset >> 8);
        memory->bytes[at + 2] = (uint8_t)segment;
        memory->bytes[at + 3] = (uint8_t)(segment >> 8);
    }
    /* An unchanged BIOS mouse callback, before the native mouse service
     * has replaced device I/O for this standalone source-C playback. */
    memory->bytes[4u * 0x33] = 0;
    memory->bytes[4u * 0x33 + 1] = 3;
    memory->bytes[4u * 0x33 + 2] = 0;
    memory->bytes[4u * 0x33 + 3] = 0xf0;
    return 1;
}
