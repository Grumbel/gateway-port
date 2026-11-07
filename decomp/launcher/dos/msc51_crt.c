/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Linked GATE.RTL 12a5:2cd0/2c38: Microsoft C getenv() and atol().
 * These two entry points deliberately call the installed Microsoft C 5.1
 * large-model C runtime. Its FAR data pointers and numeric conversion are
 * the correct ABI, unlike a native host environment or libc conversion. */
#include "GATEMEM.H"
#include <dos.h>
#include <stdlib.h>

GateFarPtr gate_linked_get_runtime_environment(GateFarPtr name)
{
    char pathname[33];
    char far *value;
    uint16_t i;
    GateFarPtr result;
    result.segment = 0u;
    result.offset = 0u;
    for (i = 0u; i < sizeof pathname; ++i) {
        pathname[i] = (char)gate_far_byte(name.segment,
                                          (uint16_t)(name.offset + i));
        if (pathname[i] == 0)
            break;
    }
    if (i == sizeof pathname)
        return result;
    value = getenv(pathname);
    if (value != 0) {
        result.segment = (uint16_t)FP_SEG(value);
        result.offset = (uint16_t)FP_OFF(value);
    }
    return result;
}

uint32_t gate_linked_parse_runtime_integer(GateFarPtr number)
{
    char far *digits = (char far *)MK_FP(number.segment, number.offset);
    return (uint32_t)atol(digits);
}
