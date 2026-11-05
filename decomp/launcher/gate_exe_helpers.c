/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_exe.h"

/* GATE.EXE:0x0bb8..0x0bc9. The apparent string-pointer normalization
 * helper merely loads its FAR argument into DX:AX and returns it. The
 * intervening far call is the Microsoft C stack-check prologue. */
GateFarPtr gate_exe_0bb8(GateFarPtr pointer)
{
    return pointer;
}
