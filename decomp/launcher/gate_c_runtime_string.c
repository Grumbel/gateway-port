/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_exe.h"

/* Original linked 12a5:2bba (DOS physical 0x1560a in the pinned intro
 * snapshot). The full routine is REPNE SCASB with CX=ffff, NOT CX, DEC CX.
 * It scans at most 65535 bytes and increments only the 16-bit FAR offset.
 * A missing NUL therefore returns 65534, not a host strlen() overflow or
 * an unbounded read across segment boundaries. The caller's DI is saved. */
static uint16_t bounded_far_strlen(GateFarPtr text)
{
    uint32_t length;
    for (length = 0; length < 0xffffu; ++length) {
        if (gate_far_byte(text.segment,
                          (uint16_t)(text.offset + length)) == 0)
            return (uint16_t)length;
    }
    return 0xfffeu;
}

uint16_t gate_far_02a5_2bba(GateFarPtr text)
{
    return bounded_far_strlen(text);
}

/* GATE.RTL file 0x8298..0x82ae is a second, byte-for-byte equivalent
 * far-strlen implementation (LES DI, SCASB with CX=ffff, NOT CX, DEC CX).
 * The raw RTL entry is proven, but its relocated linked address is not:
 * do not alias it to 02a5:2bba without a linked-memory witness. */
uint16_t gate_rtl_8298(GateFarPtr text)
{
    return bounded_far_strlen(text);
}
