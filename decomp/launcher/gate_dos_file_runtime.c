/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_rtl_file.h"
#include "gate_dos_file_runtime.h"

/* Five original resident DOS file-call wrappers at segment 1c29. These
 * addresses are live guest code entries (not RTL file offsets). Each wrapper
 * is just a register setup, INT 21h, and carry-handling prologue/epilogue.
 * The host callbacks correspond to those exact DOS operations. */

/* 1c29:0000..0027: AH=3c (create, CX=0) iff flags bit 7 is set;
 * otherwise AH=3d and AL=the low byte of flags. Any carry sets AX=0. */
uint16_t gate_far_1c29_0000(GateFarPtr filename, uint16_t flags)
{
    GateDosFileResult result = gate_dos_file_open(filename,
        (uint8_t)flags, (flags & 0x80u) != 0);
    return result.carry ? 0 : result.ax;
}

/* 1c29:0028..0035: AH=3e. The original returns AX unchanged from DOS,
 * even when carry indicates a failure. */
uint16_t gate_far_1c29_0028(uint16_t handle)
{
    return gate_dos_file_close(handle).ax;
}

/* 1c29:0036..0054: AH=3f, BX handle, DS:DX destination, CX bytes.
 * A carry forces AX=0, indistinguishable from EOF to its caller. */
uint16_t gate_far_1c29_0036(uint16_t handle, GateFarPtr destination,
                             uint16_t count)
{
    GateDosFileResult result = gate_dos_file_read(handle, destination, count);
    return result.carry ? 0 : result.ax;
}

/* 1c29:0055..0073: AH=40 with otherwise identical register layout. */
uint16_t gate_far_1c29_0055(uint16_t handle, GateFarPtr source,
                             uint16_t count)
{
    GateDosFileResult result = gate_dos_file_write(handle, source, count);
    return result.carry ? 0 : result.ax;
}

/* 1c29:0074..0093: AH=42; AL=low origin byte, CX:DX=signed offset.
 * Unlike most DOS callers this wrapper discards DX:AX on success and returns
 * zero, or 0xffff on carry. */
uint16_t gate_far_1c29_0074(uint16_t handle, uint16_t origin, uint32_t offset)
{
    GateDosFileResult result = gate_dos_file_seek(handle, (uint8_t)origin, offset);
    return result.carry ? 0xffffu : 0;
}
