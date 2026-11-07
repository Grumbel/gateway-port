/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "gate_dos_file_runtime.h"
#include "gate_dos_host_files.h"
#include "gate_rtl_file.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The native DOS adapter is deliberately separate from the decompiled C.
 * It supplies the five INT 21h functions observed in the original runtime.
 * Other guest runtime services remain explicit dependencies. */
static GateDosHostFiles *active_files;

static GateDosFileResult failure(uint16_t code)
{
    return (GateDosFileResult){code, 0, 1};
}

void gate_dos_host_files_attach(GateDosHostFiles *files)
{
    active_files = files;
}

void gate_dos_host_files_close_all(GateDosHostFiles *files)
{
    unsigned i;
    if (files == 0)
        return;
    for (i = 5; i < 256; ++i) {
        if (files->handles[i] != 0) {
            (void)fclose(files->handles[i]);
            files->handles[i] = 0;
        }
    }
    if (active_files == files)
        active_files = 0;
}

FILE *gate_dos_host_file_stream(uint16_t handle)
{
    if (active_files == 0 || handle < 5 || handle >= 256)
        return 0;
    return active_files->handles[handle];
}

/* Select the DOS basename; C:\\ and / are separators and drive prefixes
 * are stripped. No guest-supplied parent path may escape the asset directory.
 * The intro uses short 8.3 names, so an 80-byte input bound suffices. */
static int resolve_path(GateFarPtr filename, char *path, size_t capacity)
{
    char name[81];
    size_t size = 0;
    size_t start = 0;
    int written;

    if (active_files == 0 || active_files->directory == 0)
        return 0;
    while (size < 80) {
        unsigned char ch = gate_far_byte(filename.segment,
                                        (uint16_t)(filename.offset + size));
        if (ch == 0)
            break;
        if (ch < 0x20 || ch == 0x7f)
            return 0;
        name[size++] = (char)ch;
        if (ch == '/' || ch == '\\' || ch == ':')
            start = size;
    }
    if (size == 80 || start == size)
        return 0;
    name[size] = 0;
    if (strcmp(name + start, ".") == 0 || strcmp(name + start, "..") == 0)
        return 0;
    written = snprintf(path, capacity, "%s/%s", active_files->directory,
                       name + start);
    return written > 0 && (size_t)written < capacity;
}

/* The resource opener invokes the original DOS C-runtime sprintf with the
 * literal format "%s_%03d.%s" (present in the pinned original GATE.RTL).
 * This host implementation deliberately supports ONLY this known intro
 * call, not an invented full 16-bit printf implementation. The original
 * call is unbounded, but the known caller has an 80-byte output array. */
static int guest_string(GateFarPtr source, char *dest, size_t capacity)
{
    size_t i;
    for (i = 0; i < capacity; ++i) {
        uint8_t ch = gate_far_byte(source.segment,
                                   (uint16_t)(source.offset + i));
        if (ch == 0) {
            dest[i] = 0;
            return 1;
        }
        dest[i] = (char)ch;
    }
    return 0;
}

void gate_linked_format_resource_filename(GateFarPtr output, GateFarPtr format,
                         GateFarPtr base, uint16_t resource, GateFarPtr table)
{
    char spec[16];
    char prefix[64];
    char suffix[16];
    char filename[80];
    int count;
    size_t i;

    if (!guest_string(format, spec, sizeof spec) ||
        strcmp(spec, "%s_%03d.%s") != 0 ||
        !guest_string(base, prefix, sizeof prefix) ||
        !guest_string(table, suffix, sizeof suffix)) {
        gate_far_set_byte(output.segment, output.offset, 0);
        return;
    }
    count = snprintf(filename, sizeof filename, "%s_%03d.%s", prefix,
                     (int16_t)resource, suffix);
    if (count < 0 || (size_t)count >= sizeof filename) {
        gate_far_set_byte(output.segment, output.offset, 0);
        return;
    }
    for (i = 0; i <= (size_t)count; ++i)
        gate_far_set_byte(output.segment,
                          (uint16_t)(output.offset + i), (uint8_t)filename[i]);
}

/* Known caller gate_rtl_check_resource_path asks for DOS read permission (mode 4) and
 * treats return value zero as success. Unlike the original generic access
 * runtime this host endpoint only implements that one intro operation. */
uint16_t gate_linked_open_runtime_path(GateFarPtr name, uint16_t mode)
{
    char path[1024];
    FILE *file;
    if (mode != 4 || !resolve_path(name, path, sizeof path))
        return 0xffffu;
    file = fopen(path, "rb");
    if (file == 0)
        return 0xffffu;
    (void)fclose(file);
    return 0;
}

GateDosFileResult gate_dos_file_open(GateFarPtr filename, uint16_t access,
                                     uint16_t create)
{
    char path[1024];
    const char *mode;
    FILE *file;
    unsigned handle;

    if (!resolve_path(filename, path, sizeof path))
        return failure(3); /* DOS path not found */
    if (create)
        mode = "wb+";
    else if ((access & 3u) == 0)
        mode = "rb";
    else if ((access & 3u) <= 2)
        mode = "rb+";
    else
        return failure(12);
    for (handle = 5; handle < 256; ++handle)
        if (active_files->handles[handle] == 0)
            break;
    if (handle == 256)
        return failure(4);
    file = fopen(path, mode);
    if (file == 0)
        return failure(2);
    active_files->handles[handle] = file;
    return (GateDosFileResult){(uint16_t)handle, 0, 0};
}

GateDosFileResult gate_dos_file_close(uint16_t handle)
{
    int closed;
    if (active_files == 0 || handle < 5 || handle >= 256 ||
        active_files->handles[handle] == 0)
        return failure(6);
    closed = fclose(active_files->handles[handle]);
    active_files->handles[handle] = 0;
    return closed == 0 ? (GateDosFileResult){0, 0, 0} : failure(5);
}

GateDosFileResult gate_dos_file_read(uint16_t handle, GateFarPtr destination,
                                     uint16_t count)
{
    uint8_t bytes[4096];
    size_t read_total = 0;
    FILE *file;
    if (active_files == 0 || handle < 5 || handle >= 256 ||
        (file = active_files->handles[handle]) == 0)
        return failure(6);
    while (read_total < count) {
        size_t i;
        size_t batch = count - read_total;
        size_t n;
        if (batch > sizeof bytes)
            batch = sizeof bytes;
        n = fread(bytes, 1, batch, file);
        for (i = 0; i < n; ++i)
            gate_far_set_byte(destination.segment,
                              (uint16_t)(destination.offset + read_total + i), bytes[i]);
        read_total += n;
        if (n < batch)
            break;
    }
    if (ferror(file))
        return failure(5);
    return (GateDosFileResult){(uint16_t)read_total, 0, 0};
}

GateDosFileResult gate_dos_file_write(uint16_t handle, GateFarPtr source,
                                      uint16_t count)
{
    uint8_t bytes[4096];
    size_t written_total = 0;
    FILE *file;
    if (active_files == 0 || handle < 5 || handle >= 256 ||
        (file = active_files->handles[handle]) == 0)
        return failure(6);
    while (written_total < count) {
        size_t i;
        size_t batch = count - written_total;
        size_t n;
        if (batch > sizeof bytes)
            batch = sizeof bytes;
        for (i = 0; i < batch; ++i)
            bytes[i] = gate_far_byte(source.segment,
                                    (uint16_t)(source.offset + written_total + i));
        n = fwrite(bytes, 1, batch, file);
        written_total += n;
        if (n < batch)
            break;
    }
    if (ferror(file))
        return failure(5);
    return (GateDosFileResult){(uint16_t)written_total, 0, 0};
}

GateDosFileResult gate_dos_file_seek(uint16_t handle, uint16_t origin,
                                     uint32_t offset)
{
    FILE *file;
    int whence;
    long position;
    if (active_files == 0 || handle < 5 || handle >= 256 ||
        (file = active_files->handles[handle]) == 0)
        return failure(6);
    if (origin == 0)
        whence = SEEK_SET;
    else if (origin == 1)
        whence = SEEK_CUR;
    else if (origin == 2)
        whence = SEEK_END;
    else
        return failure(1);
    if (fseek(file, (long)(int32_t)offset, whence) != 0)
        return failure(1);
    position = ftell(file);
    if (position < 0)
        return failure(1);
    return (GateDosFileResult){(uint16_t)position,
                               (uint16_t)((uint32_t)position >> 16), 0};
}
