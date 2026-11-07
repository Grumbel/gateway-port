/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* DOS 21h host adapter for the source-recovered resident file wrappers.
 * This is new Microsoft C 5.1 /AL host code, not original Gateway code.
 * It reports AX, DX and carry separately, including on DOS failures.
 */
#include "GATEDOS.H"
#include <dos.h>
#include <string.h>

static GateDosFileResult gate_dos_call(union REGS *input,
                                        uint16_t data_segment, int use_data_segment)
{
    union REGS output;
    struct SREGS segments;
    GateDosFileResult result;

    /* intdosx needs both DS and ES initialized even though these services
     * only consume DS:DX. Restore the caller's other segment values.
     */
    segread(&segments);
    if (use_data_segment)
        segments.ds = data_segment;
    memset(&output, 0, sizeof output);
    intdosx(input, &output, &segments);
    result.ax = (uint16_t)output.x.ax;
    result.dx = (uint16_t)output.x.dx;
    result.carry = (uint8_t)(output.x.cflag != 0);
    return result;
}

GateDosFileResult gate_dos_file_open(GateFarPtr path, uint16_t access,
                                     uint16_t create)
{
    union REGS input;
    memset(&input, 0, sizeof input);
    input.h.ah = create ? 0x3cu : 0x3du;
    input.h.al = create ? 0 : (uint8_t)access;
    input.x.dx = path.offset;
    /* DOS 3Ch always gets CX=0 (no initial file attributes). */
    return gate_dos_call(&input, path.segment, 1);
}

GateDosFileResult gate_dos_file_close(uint16_t handle)
{
    union REGS input;
    memset(&input, 0, sizeof input);
    input.h.ah = 0x3eu;
    input.x.bx = handle;
    return gate_dos_call(&input, 0, 0);
}

GateDosFileResult gate_dos_file_read(uint16_t handle, GateFarPtr destination,
                                     uint16_t count)
{
    union REGS input;
    memset(&input, 0, sizeof input);
    input.h.ah = 0x3fu;
    input.x.bx = handle;
    input.x.cx = count;
    input.x.dx = destination.offset;
    return gate_dos_call(&input, destination.segment, 1);
}

GateDosFileResult gate_dos_file_write(uint16_t handle, GateFarPtr source,
                                      uint16_t count)
{
    union REGS input;
    memset(&input, 0, sizeof input);
    input.h.ah = 0x40u;
    input.x.bx = handle;
    input.x.cx = count;
    input.x.dx = source.offset;
    return gate_dos_call(&input, source.segment, 1);
}

GateDosFileResult gate_dos_file_seek(uint16_t handle, uint16_t origin,
                                     uint32_t offset)
{
    union REGS input;
    memset(&input, 0, sizeof input);
    input.h.ah = 0x42u;
    input.h.al = (uint8_t)origin;
    input.x.bx = handle;
    input.x.cx = (uint16_t)(offset >> 16);
    input.x.dx = (uint16_t)offset;
    return gate_dos_call(&input, 0, 0);
}
