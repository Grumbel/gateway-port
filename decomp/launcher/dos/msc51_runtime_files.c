/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* DOS/C runtime boundary for recovered GATE.RTL. These are narrow, real
 * implementations of the call shapes in gate_rtl_file.c and
 * gate_c_runtime_clock.c, not placeholder linked FAR functions.
 * The source of the bounded formatter contract is
 * src/intro_dos_host_files.c. */
#include "GATEMEM.H"
#include <io.h>
#include <stdio.h>
#include <string.h>

/* The source's resource path stack buffer is 80 bytes. Copy until NUL
 * without reading beyond a known buffer bound. Preserve 8086 offset wrap. */
static int guest_string(GateFarPtr source, char *destination, uint16_t capacity)
{
    uint16_t i;
    for (i = 0; i < capacity; ++i) {
        uint8_t ch = gate_far_byte(source.segment,
                                   (uint16_t)(source.offset + i));
        destination[i] = (char)ch;
        if (ch == 0)
            return 1;
    }
    return 0;
}

/* Observed original linked CRT sprintf call at 12a5:3366. The only
 * recovered caller passes the literal "%s_%03d.%s". Unbounded original
 * sprintf overflow is guarded at the 80-byte stack destination boundary. */
void gate_linked_format_resource_filename(GateFarPtr output, GateFarPtr format,
                            GateFarPtr base, uint16_t resource, GateFarPtr table)
{
    char spec[16];
    char prefix[64];
    char suffix[16];
    char pathname[80];
    uint16_t i;
    int count;

    if (!guest_string(format, spec, sizeof spec) ||
        strcmp(spec, "%s_%03d.%s") != 0 ||
        !guest_string(base, prefix, sizeof prefix) ||
        !guest_string(table, suffix, sizeof suffix) ||
        strlen(prefix) + strlen(suffix) + 9u > sizeof pathname) {
        gate_far_set_byte(output.segment, output.offset, 0);
        return;
    }
    count = sprintf(pathname, "%s_%03d.%s", prefix, (int16_t)resource, suffix);
    if (count < 0 || (size_t)count >= sizeof pathname) {
        gate_far_set_byte(output.segment, output.offset, 0);
        return;
    }
    for (i = 0; i <= (uint16_t)count; ++i)
        gate_far_set_byte(output.segment,
                          (uint16_t)(output.offset + i), (uint8_t)pathname[i]);
}

/* GATE.RTL:e511..e530 forwards mode 4 to the linked DOS C access() CRT.
 * Microsoft C's access(path,4) checks read access, returning 0 or -1.
 * Copy the FAR guest string into real DOS C address space before calling. */
uint16_t gate_linked_open_runtime_path(GateFarPtr name, uint16_t mode)
{
    char pathname[81];
    if (mode != 4u || !guest_string(name, pathname, sizeof pathname))
        return 0xffffu;
    return access(pathname, 4) == 0 ? 0u : 0xffffu;
}

/* The original Microsoft CRT far strncpy entry at RTL:8bb6 preserves
 * the destination FAR pointer and pads with NUL up to the requested count.
 * Both source and destination offsets wrap at 16 bits, as on the 8086. */
GateFarPtr gate_linked_copy_runtime_string(GateFarPtr destination,
                                            GateFarPtr source, uint16_t count)
{
    uint16_t i;
    int padding = 0;
    for (i = 0; i < count; ++i) {
        uint8_t value = padding ? 0 : gate_far_byte(source.segment,
                                                    (uint16_t)(source.offset + i));
        if (value == 0)
            padding = 1;
        gate_far_set_byte(destination.segment,
                          (uint16_t)(destination.offset + i), value);
    }
    return destination;
}
