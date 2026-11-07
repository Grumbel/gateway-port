/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "GATEMEM.H"
#include <dos.h>
#include <stdio.h>

GateFarPtr gate_linked_get_runtime_environment(GateFarPtr name);
uint32_t gate_linked_parse_runtime_integer(GateFarPtr number);

static void guest_string(uint16_t segment, uint16_t offset, const char *text)
{
    uint16_t i = 0;
    do {
        gate_far_set_byte(segment, (uint16_t)(offset + i), (uint8_t)text[i]);
    } while (text[i++] != 0);
}

int main(void)
{
    unsigned int segment = 0;
    int valid = 1;
    GateFarPtr path;
    GateFarPtr value;
    if (_dos_allocmem(0x1000u, &segment) != 0) {
        puts("CRT probe: no DOS memory for guest strings");
        return 1;
    }
    path.segment = (uint16_t)segment;
    path.offset = 0x0100u;
    guest_string(path.segment, path.offset, "PATH");
    value = gate_linked_get_runtime_environment(path);
    valid &= value.segment != 0 &&
             gate_far_byte(value.segment, value.offset) != 0;
    guest_string(path.segment, path.offset, "GATEWAY_MSC51_ABSENT");
    value = gate_linked_get_runtime_environment(path);
    valid &= value.segment == 0 && value.offset == 0;
    guest_string(path.segment, path.offset, "-12345junk");
    valid &= gate_linked_parse_runtime_integer(path) == (uint32_t)-12345l;
    guest_string(path.segment, path.offset, "  +37tail");
    valid &= gate_linked_parse_runtime_integer(path) == 37ul;
    if (_dos_freemem(segment) != 0)
        valid = 0;
    puts(valid ? "CRT probe: PASS" : "CRT probe: FAIL");
    return valid ? 0 : 1;
}
