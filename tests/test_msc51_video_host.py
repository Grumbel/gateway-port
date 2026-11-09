#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Execute the DOS video adapter against an independent 16-bit memory/BIOS fixture.

This is NOT a real MSC 5.1 or DOSBox test. It catches wrong RTL segment
selection, offsets, FAR indirection, packet fields and BIOS function numbers.
"""

from pathlib import Path
import signal
import shutil
import subprocess
import tempfile
import unittest


REPO = Path(__file__).resolve().parent.parent
DOS_VIDEO = REPO / "decomp/launcher/dos/msc51_hardware.c"


class MSC51VideoHostTests(unittest.TestCase):
    def test_video_segment_and_bios_command_packets(self):
        cc = shutil.which("cc")
        if cc is None:
            self.skipTest("Host C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            scratch = Path(directory)
            (scratch / "GATEMEM.H").write_text("""
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef signed short int16_t;
typedef signed char int8_t;
typedef struct GateFarPtr { uint16_t segment, offset; } GateFarPtr;
uint16_t gate_ds_segment(void);
uint16_t gate_ds_word(uint16_t offset);
uint8_t gate_far_byte(uint16_t segment, uint16_t offset);
uint16_t gate_far_word(uint16_t segment, uint16_t offset);
void gate_far_set_byte(uint16_t segment, uint16_t offset, uint8_t value);
void gate_far_set_word(uint16_t segment, uint16_t offset, uint16_t value);
void gate_ds_set_word(uint16_t offset, uint16_t value);
""")
            (scratch / "RTLLAY.H").write_text("""
#define GATE_DS_DRAW_CLIP_LEFT (0x29f8u)
#define GATE_DS_DRAW_CLIP_TOP (0x29fau)
#define GATE_DS_DRAW_CLIP_RIGHT (0x29fcu)
#define GATE_DS_DRAW_CLIP_BOTTOM (0x29feu)
#define GATE_DS_DRAW_CLIPPING_ENABLED (0x29f6u)
#define GATE_DS_PALETTE_WORK_BASE (0x0fceu)
#define GATE_DS_MUSIC_STARTUP_CONFIG (0x0660u)
#define GATE_DS_MUSIC_STARTUP_FLAGS (0x0662u)
#define GATE_STARTUP_MUSIC_TRIGGER_FLAGS_MASK (0x06u)
""")
            (scratch / "conio.h").write_text("int inp(unsigned int port);\nint outp(unsigned int port, int value);\n")
            (scratch / "dos.h").write_text("""
/* Host-only compatibility declarations: the real Microsoft C 5.1
 * interrupt FAR pointer is exercised by HWCHK.EXE in DOSBox, not here. */
#define interrupt
#define far
#define FP_SEG(pointer) (((unsigned short *)&(pointer))[1])
#define FP_OFF(pointer) (((unsigned short *)&(pointer))[0])
void _dos_setvect(unsigned int number, void (*handler)(void));
union REGS {
    struct { unsigned short ax, bx, cx, dx; } x;
    struct { unsigned char al, ah, bl, bh, cl, ch, dl, dh; } h;
};
int int86(int number, union REGS *input, union REGS *output);
""")
            (scratch / "check.c").write_text(r'''
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include "GATEMEM.H"
#include "dos.h"

static unsigned char memory[0x100000ul];
static unsigned int calls;
static unsigned int bios_set_x, bios_set_y, bios_character, bios_attribute, bios_count;
static unsigned char bios_display = 8u, bios_secondary = 0u;
static unsigned char bios_display_combination = 1u;
static unsigned char bios_svga_signature = 0u;
static unsigned char bank_port = 0x12u, crtc_index = 0x21u, crtc_33 = 0x83u;
static unsigned char bank_writable = 0u, crtc_writable = 0u;

void _dos_setvect(unsigned int number, void (*handler)(void))
{
    (void)number;
    (void)handler;
}

uint16_t gate_ds_segment(void) { return 0x4000u; }

uint8_t gate_far_byte(uint16_t segment, uint16_t offset)
{
    unsigned long address = (((unsigned long)segment << 4) + offset) & 0xffffful;
    return memory[address];
}
uint16_t gate_far_word(uint16_t segment, uint16_t offset)
{
    return (uint16_t)(gate_far_byte(segment, offset) |
                      ((uint16_t)gate_far_byte(segment, (uint16_t)(offset + 1u)) << 8));
}
uint16_t gate_ds_word(uint16_t offset)
{
    return gate_far_word(gate_ds_segment(), offset);
}
void gate_far_set_byte(uint16_t segment, uint16_t offset, uint8_t value)
{
    unsigned long address = (((unsigned long)segment << 4) + offset) & 0xffffful;
    memory[address] = value;
}
void gate_far_set_word(uint16_t segment, uint16_t offset, uint16_t value)
{
    gate_far_set_byte(segment, offset, (uint8_t)value);
    gate_far_set_byte(segment, (uint16_t)(offset + 1u), (uint8_t)(value >> 8));
}
void gate_ds_set_word(uint16_t offset, uint16_t value)
{
    gate_far_set_word(0x4000u, offset, value);
}
int inp(unsigned int port)
{
    switch (port) {
    case 0x03cdu: return bank_port;
    case 0x03d4u: return crtc_index;
    case 0x03d5u: return crtc_33;
    default: return 0x08;
    }
}
int outp(unsigned int port, int value)
{
    switch (port) {
    case 0x03cdu:
        if (bank_writable || (unsigned char)value == 0x12u)
            bank_port = (unsigned char)value;
        break;
    case 0x03d4u:
        crtc_index = (unsigned char)value;
        break;
    case 0x03d5u:
        assert(crtc_index == 0x33u);
        if (crtc_writable || (unsigned char)value == 0x83u)
            crtc_33 = (unsigned char)value;
        break;
    }
    return value;
}

int int86(int number, union REGS *input, union REGS *output)
{
    assert(number == 0x10);
    ++calls;
    *output = *input;
    switch (input->h.ah) {
    case 0x02:
        bios_set_x = input->h.dl;
        bios_set_y = input->h.dh;
        break;
    case 0x03:
        output->h.dl = 63u;
        output->h.dh = 17u;
        break;
    case 0x09:
        bios_character = input->h.al;
        bios_attribute = input->h.bl;
        bios_count = input->x.cx;
        break;
    case 0x1a:
        assert(input->h.al == 0u);
        output->h.al = bios_display_combination ? 0x1au : 0u;
        output->h.bl = bios_display;
        output->h.bh = bios_secondary;
        break;
    case 0x6f:
        assert(input->h.al == 0u);
        output->x.bx = bios_svga_signature ? 0x5637u : 0u;
        break;
    default: assert(0);
    }
    return 0;
}

void gate_dos_bind_rtl_video_ds(uint16_t segment);
uint16_t gate_rtl_video_inversion(void);
uint8_t gate_rtl_video_attribute(void);
int16_t gate_rtl_video_columns(void);
int16_t gate_rtl_video_rows(void);
int16_t gate_rtl_video_tab_base(void);
uint16_t gate_rtl_video_block_mode(void);
void gate_rtl_video_set_block_mode(uint16_t mode);
void gate_rtl_video_command(uint16_t function, uint8_t *input, uint8_t *output);
void gate_rtl_mode_query_adapter(uint8_t *adapter);
uint16_t gate_rtl_mode_svga_variant(void);
GateFarPtr gate_linked_transform_picture_palette_rgb(GateFarPtr source);
GateFarPtr gate_linked_transform_picture_palette_indexed(GateFarPtr source);
void gate_linked_shutdown_audio(void);

int main(int argc, char **argv)
{
    uint8_t packet[14];
    GateFarPtr source, colors, indices;
    uint16_t i;
    if (argc > 1) {
        /* The active device branches are not reconstructed and must
         * terminate before any partially simulated shutdown. */
        gate_ds_set_word(0x0662u, (uint16_t)(argv[1][0] == 'm' ? 0x0004u :
                           argv[1][0] == 't' ? 0x0020u : 0x0040u));
        gate_linked_shutdown_audio();
        return 123;
    }
    gate_ds_set_word(0x0660u, 0x0401u);
    gate_ds_set_word(0x0662u, 0xa08au);
    gate_linked_shutdown_audio();
    assert(gate_ds_word(0x0662u) == 0x0008u);
    assert(gate_ds_word(0x0660u) == 0x0401u);
    assert(calls == 0u);
    gate_ds_set_word(0x0662u, 0x8002u);
    gate_linked_shutdown_audio();
    assert(gate_ds_word(0x0662u) == 0u && calls == 0u);
    /* Until the original RTL text bank is bound, the source-owned cursor
     * word must be independent of the (unrelated) main EXE data segment. */
    gate_far_set_word(0x4000u, 0x030eu, 0x7777u);
    assert(gate_rtl_video_block_mode() == 0u);
    gate_rtl_video_set_block_mode(0x0102u);
    assert(gate_rtl_video_block_mode() == 0x0102u);
    assert(gate_far_word(0x4000u, 0x030eu) == 0x7777u);
    gate_dos_bind_rtl_video_ds(0x5000u);
    gate_far_set_word(0x5000u, 0x0000u, 0x2031u);
    gate_far_set_byte(0x5000u, 0x0002u, 0x7au);
    gate_far_set_word(0x5000u, 0x0004u, 0x6000u);
    gate_far_set_word(0x5000u, 0x0006u, 0x6100u);
    gate_far_set_word(0x5000u, 0x001au, 0x6200u);
    gate_far_set_word(0x6000u, 0u, 80u);
    gate_far_set_word(0x6100u, 0u, 25u);
    gate_far_set_word(0x6200u, 0u, 8u);
    gate_far_set_word(0x5000u, 0x030eu, 0xbeefu);
    assert(gate_rtl_video_inversion() == 0x2031u);
    assert(gate_rtl_video_attribute() == 0x7au);
    assert(gate_rtl_video_columns() == 80);
    assert(gate_rtl_video_rows() == 25);
    assert(gate_rtl_video_tab_base() == 8);
    assert(gate_rtl_video_block_mode() == 0xbeefu);
    gate_rtl_video_set_block_mode(0x4321u);
    assert(gate_far_word(0x5000u, 0x030eu) == 0x4321u);
    assert(gate_far_word(0x4000u, 0x030eu) != 0x4321u);

    memset(packet, 0, sizeof packet);
    packet[1] = 3u;
    gate_rtl_video_command(0x10u, packet, packet);
    assert(packet[6] == 63u && packet[7] == 17u);
    packet[1] = 2u;
    packet[6] = 21u;
    packet[7] = 4u;
    gate_rtl_video_command(0x10u, packet, packet);
    assert(bios_set_x == 21u && bios_set_y == 4u);
    packet[1] = 9u;
    packet[0] = 0xdbu;
    packet[2] = 0x87u;
    packet[4] = 0x34u;
    packet[5] = 0x12u;
    gate_rtl_video_command(0x10u, packet, packet);
    assert(bios_character == 0xdbu);
    assert(bios_attribute == 0x87u);
    assert(bios_count == 0x1234u);
    assert(calls == 3u);

    /* Original INT 10h 1a00 active and alternate adapter mappings. */
    memset(packet, 0x9au, sizeof packet);
    gate_rtl_mode_query_adapter(packet);
    assert(packet[0] == 5u && packet[1] == 5u);
    assert(packet[2] == 0u && packet[3] == 0u);
    assert(packet[4] == 0x9au);
    bios_display = 7u;
    bios_secondary = 0x0cu;
    gate_rtl_mode_query_adapter(packet);
    assert(packet[0] == 5u && packet[1] == 4u);
    assert(packet[2] == 4u && packet[3] == 5u);
    bios_display = 1u;
    bios_secondary = 8u;
    gate_rtl_mode_query_adapter(packet);
    assert(packet[0] == 0u && packet[1] == 0u);
    assert(packet[2] == 5u && packet[3] == 5u);

    bios_svga_signature = 1u;
    assert(gate_rtl_mode_svga_variant() == 3u);
    bios_svga_signature = 0u;
    gate_far_set_byte(0xc000u, 0x007du, 'V');
    gate_far_set_byte(0xc000u, 0x007eu, 'G');
    gate_far_set_byte(0xc000u, 0x007fu, 'A');
    gate_far_set_byte(0xc000u, 0x0080u, '=');
    assert(gate_rtl_mode_svga_variant() == 2u);
    gate_far_set_byte(0xc000u, 0x007du, 0u);
    bank_writable = 1u;
    crtc_writable = 1u;
    assert(gate_rtl_mode_svga_variant() == 5u);
    assert(bank_port == 0x12u && crtc_33 == 0x83u && crtc_index == 0x21u);
    crtc_writable = 0u;
    assert(gate_rtl_mode_svga_variant() == 1u);
    assert(bank_port == 0x12u && crtc_33 == 0x83u && crtc_index == 0x21u);
    bank_writable = 0u;
    gate_far_set_byte(0xc000u, 0x0040u, '3');
    gate_far_set_byte(0xc000u, 0x0041u, '1');
    assert(gate_rtl_mode_svga_variant() == 4u);
    gate_far_set_byte(0xc000u, 0x0040u, 0u);
    assert(gate_rtl_mode_svga_variant() == 0u);

    /* Two original palette functions at RTL:f4df/f579, with wrapped FAR
     * input offsets, 48-byte RGB output and 16-byte packed output. */
    source.segment = 0x5100u;
    source.offset = 0xfff8u;
    for (i = 0; i < 16u; ++i)
        gate_far_set_byte(source.segment, (uint16_t)(source.offset + i),
                          (uint8_t)((i * 5u) & 63u));
    colors = gate_linked_transform_picture_palette_rgb(source);
    assert(colors.segment == 0x4000u && colors.offset == 0x0fd8u);
    assert(gate_far_byte(colors.segment, colors.offset) == 0u);
    assert(gate_far_byte(colors.segment, (uint16_t)(colors.offset + 2u)) == 0u);
    indices = gate_linked_transform_picture_palette_indexed(colors);
    assert(indices.segment == 0x4000u && indices.offset == 0x1008u);
    for (i = 0; i < 16u; ++i)
        assert(gate_far_byte(indices.segment, (uint16_t)(indices.offset + i)) ==
               (uint8_t)((i * 5u) & 63u));
    gate_far_set_byte(source.segment, source.offset, 63u);
    colors = gate_linked_transform_picture_palette_rgb(source);
    assert(gate_far_byte(colors.segment, colors.offset) == 63u);
    assert(gate_far_byte(colors.segment, (uint16_t)(colors.offset + 1u)) == 63u);
    assert(gate_far_byte(colors.segment, (uint16_t)(colors.offset + 2u)) == 63u);
    gate_far_set_byte(source.segment, source.offset, 0x01u);
    colors = gate_linked_transform_picture_palette_rgb(source);
    assert(gate_far_byte(colors.segment, colors.offset) == 0u);
    assert(gate_far_byte(colors.segment, (uint16_t)(colors.offset + 1u)) == 0u);
    assert(gate_far_byte(colors.segment, (uint16_t)(colors.offset + 2u)) == 42u);
    gate_far_set_byte(source.segment, source.offset, 0x20u);
    colors = gate_linked_transform_picture_palette_rgb(source);
    assert(gate_far_byte(colors.segment, colors.offset) == 21u);
    assert(gate_far_byte(colors.segment, (uint16_t)(colors.offset + 1u)) == 0u);
    assert(gate_far_byte(colors.segment, (uint16_t)(colors.offset + 2u)) == 0u);
    gate_far_set_byte(colors.segment, colors.offset, 63u);
    gate_far_set_byte(colors.segment, (uint16_t)(colors.offset + 1u), 21u);
    gate_far_set_byte(colors.segment, (uint16_t)(colors.offset + 2u), 42u);
    indices = gate_linked_transform_picture_palette_indexed(colors);
    assert(gate_far_byte(indices.segment, indices.offset) == 0x35u);
    return 0;
}
''')
            binary = scratch / "video-test"
            build = subprocess.run(
                [cc, "-std=c89", "-Wall", "-Wextra", "-Werror", "-pedantic", "-I", str(scratch),
                 str(DOS_VIDEO), str(scratch / "check.c"), "-o", str(binary)],
                capture_output=True, text=True,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            tested = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(tested.returncode, 0, tested.stdout + tested.stderr)
            for active in ("music", "timer", "device"):
                unsupported = subprocess.run([str(binary), active],
                                             capture_output=True, text=True,
                                             cwd=str(scratch))
                self.assertEqual(unsupported.returncode, -signal.SIGABRT,
                                 unsupported.stdout + unsupported.stderr)


if __name__ == "__main__":
    unittest.main()
