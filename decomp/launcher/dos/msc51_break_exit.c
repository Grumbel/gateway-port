/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* DOS boundary for the original RTL:621e runtime termination callee.
 * MS C 5.1 exit() runs its own installed CRT shutdown and terminates the
 * DOS process; it is NOT a claimed decompilation of RTL:621e..62bb. */
#include "GATEMEM.H"
#include <stdlib.h>

void gate_linked_crt_exit(uint16_t status)
{
    exit((int)(uint8_t)status);
}
