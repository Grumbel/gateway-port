/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

/* Original GATE.EXE startup observation: at 100ms..1500ms the 5976
 * bytes at EXE:041e6 are copied identically to the linked runtime
 * DS:3592. The adjacent bytes do not belong to this exact copy.
 *
 * This is only ONE source-image copy. No original data is embedded and
 * the caller must already own the two overlapping segment windows and
 * an open handle to the original GATE.EXE. It neither loads RTL banks
 * nor applies fixups, creates BSS, or implements a DOS launcher main().
 */
#include "GATEDOS.H"

#define GATE_STATIC_EXE_SOURCE 0x000041e6ul
#define GATE_STATIC_DATA_OFFSET 0x3592u
#define GATE_STATIC_DATA_BYTES 0x1758u
#define GATE_STATIC_DATA_REQUIRED_PARAGRAPHS 0x100au

/* -1 invalid allocation/binding, -2 failed/inexact seek, -3 read error
 * or short read. On a short read, the destination may be partly written;
 * callers must discard that allocation rather than entering the intro.
 * Does not close the caller-owned handle. */
int gate_dos_load_intro_static_data(uint16_t handle, uint16_t lower_segment,
                                    uint16_t paragraphs)
{
    GateDosFileResult result;
    GateFarPtr target;

    if (lower_segment == 0u || lower_segment > 0xfff5u ||
        paragraphs < GATE_STATIC_DATA_REQUIRED_PARAGRAPHS ||
        gate_ds_segment() != (uint16_t)(lower_segment + 0x000au))
        return -1;

    result = gate_dos_file_seek(handle, 0u, GATE_STATIC_EXE_SOURCE);
    if (result.carry || result.ax != 0x41e6u || result.dx != 0u)
        return -2;

    target.segment = gate_ds_segment();
    target.offset = GATE_STATIC_DATA_OFFSET;
    result = gate_dos_file_read(handle, target, GATE_STATIC_DATA_BYTES);
    if (result.carry || result.ax != GATE_STATIC_DATA_BYTES)
        return -3;
    return 0;
}
