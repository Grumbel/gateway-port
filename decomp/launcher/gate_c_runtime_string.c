/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_exe.h"

/* Original linked 12a5:2bba (DOS physical 0x1560a in the pinned intro
 * snapshot). The full routine is REPNE SCASB with CX=ffff, NOT CX, DEC CX.
 * It scans at most 65535 bytes and increments only the 16-bit FAR offset.
 * A missing NUL therefore returns 65534, not a host strlen() overflow or
 * an unbounded read across segment boundaries. The caller's DI is saved. */
uint16_t gate_far_02a5_2bba(GateFarPtr text)
{
    uint32_t length;
    for (length = 0; length < 0xffffu; ++length) {
        if (gate_far_byte(text.segment,
                          (uint16_t)(text.offset + length)) == 0)
            return (uint16_t)length;
    }
    return 0xfffeu;
}
