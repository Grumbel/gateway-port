/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_exe.h"
#include "gate_c_runtime_constants.h"
#include "gate_rtl.h"

/* Original GATE.RTL:8298, linked at 12a5:2bba (DOS physical 0x1560a in
 * the 22-second snapshot). All 23 code bytes match the original RTL source.
 * The full routine is REPNE SCASB with CX=ffff, NOT CX, DEC CX.
 * It scans at most 65535 bytes and increments only the 16-bit FAR offset.
 * A missing NUL therefore returns 65534, not a host strlen() overflow or
 * an unbounded read across segment boundaries. The caller's DI is saved. */
static uint16_t bounded_far_strlen(GateFarPtr text)
{
    uint32_t length;
    for (length = 0; length < GATE_CRT_FAR_STRING_SCAN_LIMIT; ++length) {
        if (gate_far_byte(text.segment,
                          (uint16_t)(text.offset + length)) == 0)
            return (uint16_t)length;
    }
    return GATE_CRT_FAR_STRING_UNTERMINATED_LENGTH;
}

uint16_t gate_linked_exe_string_length(GateFarPtr text)
{
    return gate_rtl_string_length(text);
}

/* GATE.RTL file 0x8298..0x82ae: LES DI, SCASB with CX=ffff,
 * NOT CX, DEC CX; source identity and linked address are both verified. */
uint16_t gate_rtl_string_length(GateFarPtr text)
{
    return bounded_far_strlen(text);
}
