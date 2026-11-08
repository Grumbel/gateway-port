/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* A DOS real-mode executable verifies filenames, file access and FAR
 * strncpy in the same /AL model used by the recovered launcher modules. */
#include "GATEMEM.H"
#include <dos.h>
#include <stdio.h>
#include <string.h>

void gate_linked_format_resource_filename(GateFarPtr output, GateFarPtr format,
                          GateFarPtr base, uint16_t resource, GateFarPtr table);
uint16_t gate_linked_open_runtime_path(GateFarPtr name, uint16_t mode);
GateFarPtr gate_linked_copy_runtime_string(GateFarPtr destination,
                                           GateFarPtr source, uint16_t length);
void gate_linked_print_intro_diagnostic(GateFarPtr message);

static GateFarPtr pointer(uint16_t segment, uint16_t offset)
{
    GateFarPtr result;
    result.segment = segment;
    result.offset = offset;
    return result;
}

static void put_string(GateFarPtr destination, const char *text)
{
    uint16_t i;
    for (i = 0; ; ++i) {
        uint8_t ch = (uint8_t)text[i];
        gate_far_set_byte(destination.segment,
                          (uint16_t)(destination.offset + i), ch);
        if (ch == 0)
            return;
    }
}

static int check(uint16_t actual, uint16_t expected, const char *label)
{
    if (actual != expected) {
        printf("Runtime file probe: %s got %04x expected %04x\n",
               label, actual, expected);
        return 0;
    }
    return 1;
}

static int equals_string(GateFarPtr location, const char *expected)
{
    uint16_t i;
    for (i = 0; expected[i] != 0; ++i)
        if (gate_far_byte(location.segment, (uint16_t)(location.offset + i)) !=
            (uint8_t)expected[i])
            return 0;
    return gate_far_byte(location.segment,
                         (uint16_t)(location.offset + i)) == 0;
}

int main(void)
{
    unsigned int segment = 0;
    GateFarPtr output, format, base, suffix, target, source, result;
    FILE *file;
    int valid = 1;

    if (_dos_allocmem(0x1000u, &segment) != 0) {
        puts("Runtime file probe: DOS memory allocation failed");
        return 1;
    }
    gate_dos_bind_ds((uint16_t)segment);
    output = pointer((uint16_t)segment, 0x1000u);
    format = pointer((uint16_t)segment, 0x1100u);
    base = pointer((uint16_t)segment, 0x1200u);
    suffix = pointer((uint16_t)segment, 0x1300u);
    put_string(format, "%s_%03d.%s");
    put_string(base, "GATE");
    /* A literal percent sign must not be treated as a CRT format string. */
    put_string(output, "DOS diagnostic: 100% literal\n");
    gate_linked_print_intro_diagnostic(output);
    put_string(suffix, "RS");
    gate_linked_format_resource_filename(output, format, base, 7u, suffix);
    valid &= check(equals_string(output, "GATE_007.RS"), 1u, "resource filename");
    gate_linked_format_resource_filename(output, format, base, 0xfffeu, suffix);
    valid &= check(equals_string(output, "GATE_-02.RS"), 1u, "signed resource number");
    put_string(format, "%s");
    gate_linked_format_resource_filename(output, format, base, 7u, suffix);
    valid &= check(gate_far_byte(output.segment, output.offset), 0u,
                   "unsupported format rejected");
    put_string(format, "%s_%03d.%s");

    source = pointer((uint16_t)segment, 0x1400u);
    target = pointer((uint16_t)segment, 0x1500u);
    put_string(source, "AB");
    result = gate_linked_copy_runtime_string(target, source, 5u);
    valid &= check(result.segment, target.segment, "strncpy destination segment");
    valid &= check(result.offset, target.offset, "strncpy destination offset");
    valid &= check(gate_far_byte(target.segment, target.offset), 'A', "strncpy first byte");
    valid &= check(gate_far_byte(target.segment, (uint16_t)(target.offset + 1)), 'B',
                   "strncpy second byte");
    valid &= check(gate_far_byte(target.segment, (uint16_t)(target.offset + 2)), 0u,
                   "strncpy terminator");
    valid &= check(gate_far_byte(target.segment, (uint16_t)(target.offset + 4)), 0u,
                   "strncpy padding");
    put_string(source, "LONGER");
    result = gate_linked_copy_runtime_string(target, source, 3u);
    valid &= check(gate_far_byte(target.segment, (uint16_t)(target.offset + 2)), 'N',
                   "strncpy bounded copy");
    valid &= check(gate_far_byte(target.segment, (uint16_t)(target.offset + 3)), 0u,
                   "strncpy boundary intact");
    put_string(source, "XY");
    target = pointer((uint16_t)segment, 0xffffu);
    result = gate_linked_copy_runtime_string(target, source, 3u);
    valid &= check(result.offset, 0xffffu, "strncpy wrap destination return");
    valid &= check(gate_far_byte(target.segment, 0xffffu), 'X', "strncpy wrapped first");
    valid &= check(gate_far_byte(target.segment, 0u), 'Y', "strncpy wrapped second");
    valid &= check(gate_far_byte(target.segment, 1u), 0u, "strncpy wrapped NUL");

    put_string(output, "GWTST.TMP");
    file = fopen("GWTST.TMP", "wb");
    if (file == 0) {
        puts("Runtime file probe: cannot create temporary DOS file");
        _dos_freemem(segment);
        return 1;
    }
    fclose(file);
    valid &= check(gate_linked_open_runtime_path(output, 4u), 0u,
                   "existing DOS file readable");
    valid &= check(gate_linked_open_runtime_path(output, 2u), 0xffffu,
                   "unsupported mode rejected");
    remove("GWTST.TMP");
    valid &= check(gate_linked_open_runtime_path(output, 4u), 0xffffu,
                   "missing DOS file rejected");

    if (_dos_freemem(segment) != 0) {
        puts("Runtime file probe: DOS memory free failed");
        return 1;
    }
    puts(valid ? "Runtime file probe: PASS" : "Runtime file probe: FAIL");
    return valid ? 0 : 1;
}
