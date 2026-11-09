#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Test real-mode resident raster glue with a checked 8086-memory/BIOS model.

The DOS source is tested directly. Genuine VGA and Microsoft C 5.1 still
need user-side verification with the separately generated RESCHK.EXE.
"""

from pathlib import Path
import re
import shutil
import signal
import subprocess
import tempfile
import unittest


REPO = Path(__file__).resolve().parent.parent
DOS_RASTER = REPO / "decomp/launcher/dos/msc51_resident_state.c"


class MSC51RasterHostTests(unittest.TestCase):
    def test_mode_13_and_planar_pixels_lines_and_rectangles(self):
        cc = shutil.which("cc")
        if cc is None:
            self.skipTest("Host C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            scratch = Path(directory)
            staged = scratch / "staged"
            result = subprocess.run(
                ["python3", str(REPO / "tools/msc51.py"), "prepare-launcher",
                 "--output", str(staged)], capture_output=True, text=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            # The DOS stage renames >31-character identifiers for MSC 5.1.
            # This host test compiles the unchanged original DOS source and
            # therefore needs the unshortened, equivalent layout header.
            original_layout = (REPO / "decomp/launcher/gate_memory_layout.h").read_text()
            (scratch / "RTLLAY.H").write_text(re.sub(r",(\s*})", r"\1", original_layout))
            (scratch / "conio.h").write_text("int inp(unsigned int port);\nint outp(unsigned int port, int value);\n")
            (scratch / "memory.h").write_text("void movedata(unsigned int srcseg, unsigned int srcoff, unsigned int dstseg, unsigned int dstoff, unsigned int count);\n")
            (scratch / "dos.h").write_text("""
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
#include "RTLLAY.H"
#include "dos.h"

static unsigned char memory[0x100000ul];
static unsigned char planar[640ul * 480ul];
static uint16_t video_mode = 3u;
static uint16_t current_ds = 0x3000u;
static unsigned int mouse_hides;
static unsigned int mouse_shows;
static unsigned int movedata_calls;
static unsigned int movedata_bytes;
static unsigned int planar_bios_reads;
static unsigned int planar_bios_writes;
static unsigned int planar_vga_writes;
static uint8_t gc_register[9] = { 0u, 0u, 0u, 0u, 0u, 0u, 0u, 15u, 255u };
static uint8_t gc_index;
static uint8_t seq_register[5] = { 0u, 0u, 15u, 0u, 0u };
static uint8_t seq_index;
static uint8_t vga_latches[4];

static uint8_t plane_byte(unsigned int plane, unsigned int offset)
{
    unsigned int bit;
    uint8_t result = 0u;
    for (bit = 0u; bit < 8u; ++bit) {
        unsigned int pixel = (offset / 80u) * 640u +
                             (offset % 80u) * 8u + bit;
        if ((planar[pixel] & (1u << plane)) != 0u)
            result |= (uint8_t)(0x80u >> bit);
    }
    return result;
}

static void vga_write(unsigned int offset, uint8_t data)
{
    unsigned int plane, bit;
    unsigned int logic = (gc_register[3] >> 3) & 3u;
    ++planar_vga_writes;
    assert(gc_register[5] == 8u && gc_register[7] == 0u);
    for (plane = 0u; plane < 4u; ++plane) {
        if ((seq_register[2] & (1u << plane)) == 0u)
            continue;
        for (bit = 0u; bit < 8u; ++bit) {
            unsigned int index = (offset / 80u) * 640u +
                                 (offset % 80u) * 8u + bit;
            uint8_t mask = (uint8_t)(0x80u >> bit);
            uint8_t src = (data & mask) != 0u;
            uint8_t old = (vga_latches[plane] & mask) != 0u;
            uint8_t value = src;
            if ((gc_register[8] & mask) == 0u)
                value = old;
            else if (logic == 1u)
                value = (uint8_t)(src & old);
            else if (logic == 2u)
                value = (uint8_t)(src | old);
            else if (logic == 3u)
                value = (uint8_t)(src ^ old);
            if (value)
                planar[index] |= (uint8_t)(1u << plane);
            else
                planar[index] &= (uint8_t)~(1u << plane);
        }
    }
}

uint16_t gate_ds_segment(void) { return current_ds; }
uint8_t gate_far_byte(uint16_t segment, uint16_t offset)
{
    unsigned long address = (((unsigned long)segment << 4) + offset) & 0xffffful;
    if (segment == 0xa000u && video_mode == 0x12u) {
        unsigned int plane;
        for (plane = 0u; plane < 4u; ++plane)
            vga_latches[plane] = plane_byte(plane, offset);
        assert(gc_register[5] == 8u && gc_register[7] == 0u);
        return 0xffu; /* VGA read mode 1 with color-don't-care = 0 */
    }
    return memory[address];
}
uint16_t gate_far_word(uint16_t segment, uint16_t offset)
{
    return (uint16_t)(gate_far_byte(segment, offset) |
                      ((uint16_t)gate_far_byte(segment, (uint16_t)(offset + 1u)) << 8));
}
void gate_far_set_byte(uint16_t segment, uint16_t offset, uint8_t value)
{
    unsigned long address = (((unsigned long)segment << 4) + offset) & 0xffffful;
    if (segment == 0xa000u && video_mode == 0x12u) {
        vga_write(offset, value);
        return;
    }
    memory[address] = value;
}
void gate_far_set_word(uint16_t segment, uint16_t offset, uint16_t value)
{
    gate_far_set_byte(segment, offset, (uint8_t)value);
    gate_far_set_byte(segment, (uint16_t)(offset + 1u), (uint8_t)(value >> 8));
}
/* Model MSC 5.1 movedata's 16-bit segment:offset copying for the original
 * RTL:189ca REP MOVSW plus odd-byte REP MOVSB. */
void movedata(unsigned int srcseg, unsigned int srcoff,
              unsigned int dstseg, unsigned int dstoff, unsigned int count)
{
    unsigned int i;
    ++movedata_calls;
    movedata_bytes += count;
    for (i = 0u; i < count; ++i)
        gate_far_set_byte((uint16_t)dstseg, (uint16_t)(dstoff + i),
                          gate_far_byte((uint16_t)srcseg, (uint16_t)(srcoff + i)));
}
uint16_t gate_ds_word(uint16_t offset) { return gate_far_word(current_ds, offset); }
void gate_ds_set_word(uint16_t offset, uint16_t value)
{
    gate_far_set_word(current_ds, offset, value);
}
/* This host raster fixture does not allocate DOS heap blocks. The real
 * destructor is exercised by the independent MSC 5.1 resident-state probe. */
uint16_t gate_linked_release_vfx_pixels(GateFarPtr pointer)
{
    (void)pointer;
    assert(0 && "Unexpected DOS pixel release in raster-only fixture");
    return 0xffffu;
}
int inp(unsigned int port) { (void)port; return 0u; }
int outp(unsigned int port, int value)
{
    if (port == 0x3ceu)
        gc_index = (uint8_t)value;
    else if (port == 0x3cfu) {
        assert(gc_index < sizeof(gc_register));
        gc_register[gc_index] = (uint8_t)value;
    } else if (port == 0x3c4u)
        seq_index = (uint8_t)value;
    else if (port == 0x3c5u) {
        assert(seq_index < sizeof(seq_register));
        seq_register[seq_index] = (uint8_t)value;
    } else
        assert(0);
    return value;
}
int int86(int number, union REGS *input, union REGS *output)
{
    if (number == 0x33) {
        if (input->x.ax == 2u)
            ++mouse_hides;
        else if (input->x.ax == 1u)
            ++mouse_shows;
        else
            assert(0);
        *output = *input;
        return 0;
    }
    assert(number == 0x10);
    *output = *input;
    switch (input->h.ah) {
    case 0x00u:
        video_mode = input->h.al;
        break;
    case 0x0fu:
        output->h.al = (uint8_t)video_mode;
        break;
    case 0x0du:
        assert(input->x.cx < 640u && input->x.dx < 480u);
        ++planar_bios_reads;
        output->h.al = planar[(unsigned long)input->x.dx * 640u + input->x.cx];
        break;
    case 0x0cu:
        assert(input->x.cx < 640u && input->x.dx < 480u);
        ++planar_bios_writes;
        planar[(unsigned long)input->x.dx * 640u + input->x.cx] = input->h.al;
        break;
    default:
        assert(0);
    }
    return 0;
}

void gate_rtl_plot_pixel(uint16_t color, uint16_t y, uint16_t x);
void gate_rtl_resident_move_to(uint16_t x, uint16_t y);
void gate_rtl_resident_line_to(uint16_t x, uint16_t y);
void gate_rtl_background_setup(uint16_t pattern, uint16_t color, uint16_t transparent);
void gate_rtl_background_rect(uint16_t style, uint16_t bottom, uint16_t right,
                              uint16_t top, uint16_t left);
void gate_linked_blit_vfx_picture(uint16_t zero, uint16_t y, uint16_t x,
                                  uint16_t operation, GateFarPtr image);
uint16_t gate_linked_save_vfx_draw_state(void);
void gate_linked_restore_vfx_draw_state(uint16_t saved);

static uint8_t pixel13(uint16_t x, uint16_t y)
{
    return gate_far_byte(0xa000u, (uint16_t)(320ul * y + x));
}

int main(int argc, char **argv)
{
    GateFarPtr image;
    if (argc > 1) {
        /* The backing-buffer branch is deliberately unimplemented;
         * ensure it never silently returns a fake successful save. */
        (void)argv;
        gate_ds_set_word(GATE_DS_VFX_STATE_FLAGS, 0x000du);
        gate_linked_save_vfx_draw_state();
        return 123;
    }
    /* The original save/restore entry examines the full WORD. A save
     * that clears the only set bit leaves no state to restore. */
    gate_ds_set_word(GATE_DS_VFX_STATE_FLAGS, 0u);
    assert(gate_linked_save_vfx_draw_state() == 0u);
    gate_linked_restore_vfx_draw_state(1u);
    assert(gate_ds_word(GATE_DS_VFX_STATE_FLAGS) == 0u);
    gate_ds_set_word(GATE_DS_VFX_STATE_FLAGS, 1u);
    assert(gate_linked_save_vfx_draw_state() == 1u);
    gate_linked_restore_vfx_draw_state(1u);
    assert(gate_ds_word(GATE_DS_VFX_STATE_FLAGS) == 0u);

    gate_ds_set_word(GATE_DS_VFX_STATE_FLAGS, 0x8041u);
    assert(gate_linked_save_vfx_draw_state() == 1u);
    assert(gate_ds_word(GATE_DS_VFX_STATE_FLAGS) == 0x8040u);
    gate_linked_restore_vfx_draw_state(0u);
    assert(gate_ds_word(GATE_DS_VFX_STATE_FLAGS) == 0x8040u);
    gate_linked_restore_vfx_draw_state(1u);
    assert(gate_ds_word(GATE_DS_VFX_STATE_FLAGS) == 0x8041u);

    /* INT 33h AX=2/1 calls are paired even if the upper bit is set.
     * When both 0x30 and 0x0c are set, the mouse branch wins. */
    gate_ds_set_word(GATE_DS_VFX_STATE_FLAGS, 0x803du);
    assert(gate_linked_save_vfx_draw_state() == 1u);
    assert(gate_ds_word(GATE_DS_VFX_STATE_FLAGS) == 0x803cu);
    assert(mouse_hides == 1u && mouse_shows == 0u);
    gate_linked_restore_vfx_draw_state(1u);
    assert(gate_ds_word(GATE_DS_VFX_STATE_FLAGS) == 0x803du);
    assert(mouse_hides == 1u && mouse_shows == 1u);

    gate_ds_set_word(GATE_DS_RENDERER_MODE, 3u);
    gate_ds_set_word(GATE_DS_DRAW_CLIPPING_ENABLED, 0u);
    gate_ds_set_word(GATE_DS_DRAW_OPERATION, 0u);
    gate_rtl_plot_pixel(0x12u, 8u, 9u);
    assert(pixel13(9u, 8u) == 0x12u);
    gate_ds_set_word(GATE_DS_DRAW_OPERATION, 3u);
    gate_rtl_plot_pixel(0x0fu, 8u, 9u);
    assert(pixel13(9u, 8u) == 0x1du);
    gate_ds_set_word(GATE_DS_DRAW_OPERATION, 1u);
    gate_rtl_plot_pixel(0x07u, 8u, 9u);
    assert(pixel13(9u, 8u) == 0x05u);
    gate_ds_set_word(GATE_DS_DRAW_OPERATION, 2u);
    gate_rtl_plot_pixel(0x80u, 8u, 9u);
    assert(pixel13(9u, 8u) == 0x85u);

    gate_ds_set_word(GATE_DS_DRAW_OPERATION, 0u);
    gate_ds_set_word(GATE_DS_DRAW_CLIPPING_ENABLED, 1u);
    gate_ds_set_word(GATE_DS_DRAW_CLIP_LEFT, 10u);
    gate_ds_set_word(GATE_DS_DRAW_CLIP_RIGHT, 20u);
    gate_ds_set_word(GATE_DS_DRAW_CLIP_TOP, 10u);
    gate_ds_set_word(GATE_DS_DRAW_CLIP_BOTTOM, 20u);
    gate_rtl_plot_pixel(0x33u, 8u, 9u);
    assert(pixel13(9u, 8u) == 0x85u);
    gate_rtl_plot_pixel(0x34u, 10u, 10u);
    assert(pixel13(10u, 10u) == 0x34u);
    gate_ds_set_word(GATE_DS_DRAW_CLIPPING_ENABLED, 0u);
    gate_ds_set_word(GATE_DS_DRAW_LINE_WIDTH, 1u);
    gate_ds_set_word(GATE_DS_DRAW_LINE_MASK, 0xffffu);
    gate_ds_set_word(GATE_DS_DRAW_COLOR, 0x11u);
    gate_rtl_resident_move_to(5u, 5u);
    gate_rtl_resident_line_to(8u, 5u);
    assert(pixel13(5u, 5u) == 0x11u);
    assert(pixel13(6u, 5u) == 0x11u);
    assert(pixel13(8u, 5u) == 0x11u);
    assert(gate_ds_word(GATE_DS_DRAW_X) == 8u);
    gate_rtl_resident_line_to(8u, 7u);
    assert(pixel13(8u, 6u) == 0x11u);
    gate_rtl_background_setup(0u, 0x24u, 0u);
    gate_rtl_background_rect(2u, 14u, 14u, 12u, 12u);
    assert(pixel13(12u, 12u) == 0x24u);
    assert(pixel13(13u, 13u) == 0x24u);
    assert(pixel13(14u, 14u) == 0x24u);
    assert(gate_ds_word(GATE_DS_DRAW_COLOR) == 0x11u);
    assert(gate_ds_word(GATE_DS_DRAW_LINE_WIDTH) == 1u);
    gate_rtl_background_rect(1u, 19u, 19u, 17u, 17u);
    assert(pixel13(18u, 17u) == 0x11u);
    assert(pixel13(17u, 18u) == 0x11u);
    assert(pixel13(18u, 18u) == 0u);
    assert(gate_ds_word(GATE_DS_DRAW_X) == 8u);
    assert(gate_ds_word(GATE_DS_DRAW_Y) == 7u);

    gate_ds_set_word(GATE_DS_RENDERER_MODE, 2u);
    video_mode = 0x12u;
    gate_rtl_plot_pixel(0x1bu, 16u, 18u);
    assert(planar[16ul * 640u + 18u] == 0x0bu);
    gate_ds_set_word(GATE_DS_DRAW_OPERATION, 3u);
    gate_rtl_plot_pixel(0x07u, 16u, 18u);
    assert(planar[16ul * 640u + 18u] == 0x0cu);

    /* A real CA00 VFX image record, first in mode 13h, then 12h.
     * Source pixel buffers are separate from the VGA aperture. */
    image.segment = 0x6000u;
    image.offset = 0x0500u;
    gate_far_set_word(image.segment, image.offset, 0xca00u);
    gate_far_set_word(image.segment, image.offset + 0x2cu, 2u);
    gate_far_set_word(image.segment, image.offset + 0x2eu, 2u);
    gate_far_set_word(image.segment, image.offset + 0x30u, 2u);
    gate_far_set_word(image.segment, image.offset + 0x14u, 2u);
    gate_far_set_word(image.segment, image.offset + 0x18u, 0x0200u);
    gate_far_set_word(image.segment, image.offset + 0x1au, 0x6100u);
    gate_far_set_byte(image.segment, image.offset + 0x12u, 1u);
    gate_far_set_byte(image.segment, image.offset + 0x13u, 8u);
    gate_far_set_byte(0x6100u, 0x0200u, 0x13u);
    gate_far_set_byte(0x6100u, 0x0201u, 0x24u);
    gate_far_set_byte(0x6100u, 0x0202u, 0x35u);
    gate_far_set_byte(0x6100u, 0x0203u, 0x46u);
    gate_ds_set_word(GATE_DS_RENDERER_MODE, 3u);
    video_mode = 0x13u;
    gate_linked_blit_vfx_picture(0u, 20u, 30u, 0u, image);
    assert(movedata_calls == 2u && movedata_bytes == 4u);
    assert(pixel13(30u, 20u) == 0x13u);
    assert(pixel13(31u, 20u) == 0x24u);
    assert(pixel13(30u, 21u) == 0x35u);
    assert(pixel13(31u, 21u) == 0x46u);
    gate_linked_blit_vfx_picture(0u, 20u, 30u, 3u, image);
    assert(pixel13(30u, 20u) == 0u);
    assert(pixel13(31u, 21u) == 0u);
    assert(movedata_calls == 2u); /* XOR must not use the copy branch. */

    /* Original RTL:189d1..189d3 also copies odd row widths. Verify that
     * each row still starts at the image stride and screen pitch. */
    gate_far_set_word(image.segment, image.offset + 0x2cu, 3u);
    gate_far_set_word(image.segment, image.offset + 0x30u, 3u);
    gate_far_set_word(image.segment, image.offset + 0x14u, 4u);
    gate_far_set_byte(0x6100u, 0x0200u, 0x51u);
    gate_far_set_byte(0x6100u, 0x0201u, 0x52u);
    gate_far_set_byte(0x6100u, 0x0202u, 0x53u);
    gate_far_set_byte(0x6100u, 0x0204u, 0x61u);
    gate_far_set_byte(0x6100u, 0x0205u, 0x62u);
    gate_far_set_byte(0x6100u, 0x0206u, 0x63u);
    gate_linked_blit_vfx_picture(0u, 22u, 32u, 0u, image);
    assert(movedata_calls == 4u && movedata_bytes == 10u);
    assert(pixel13(32u, 22u) == 0x51u);
    assert(pixel13(33u, 22u) == 0x52u);
    assert(pixel13(34u, 22u) == 0x53u);
    assert(pixel13(32u, 23u) == 0x61u);
    assert(pixel13(34u, 23u) == 0x63u);

    gate_ds_set_word(GATE_DS_RENDERER_MODE, 2u);
    video_mode = 0x12u;
    gate_far_set_word(image.segment, image.offset + 0x2eu, 1u);
    gate_far_set_word(image.segment, image.offset + 0x14u, 1u);
    gate_far_set_byte(image.segment, image.offset + 0x12u, 4u);
    gate_far_set_byte(image.segment, image.offset + 0x13u, 1u);
    gate_far_set_byte(0x6100u, 0x0200u, 0x80u); /* plane 0, x=0 */
    gate_far_set_byte(0x6100u, 0x0201u, 0x40u); /* plane 1, x=1 */
    gate_far_set_byte(0x6100u, 0x0202u, 0x80u); /* plane 2, x=0 */
    gate_far_set_byte(0x6100u, 0x0203u, 0u);
    gate_linked_blit_vfx_picture(0u, 16u, 18u, 0u, image);
    assert(planar[16ul * 640u + 18u] == 5u);
    assert(planar[16ul * 640u + 19u] == 2u);

    /* Recovered RTL:18870 aligned copy: 4 planes x 2 rows x 1 byte.
     * Source rows are plane-interleaved with a two-byte plane stride. */
    gate_far_set_word(image.segment, image.offset + 0x2cu, 8u);
    gate_far_set_word(image.segment, image.offset + 0x2eu, 2u);
    gate_far_set_word(image.segment, image.offset + 0x30u, 1u);
    gate_far_set_word(image.segment, image.offset + 0x14u, 2u);
    gate_far_set_byte(0x6100u, 0x0200u, 0x80u);
    gate_far_set_byte(0x6100u, 0x0202u, 0x40u);
    gate_far_set_byte(0x6100u, 0x0204u, 0x20u);
    gate_far_set_byte(0x6100u, 0x0206u, 0x10u);
    gate_far_set_byte(0x6100u, 0x0208u, 0x55u);
    gate_far_set_byte(0x6100u, 0x020au, 0xaau);
    gate_far_set_byte(0x6100u, 0x020cu, 0u);
    gate_far_set_byte(0x6100u, 0x020eu, 0xf0u);
    {
        unsigned int prior_bios_reads = planar_bios_reads;
        unsigned int prior_bios_writes = planar_bios_writes;
        gate_linked_blit_vfx_picture(0u, 18u, 32u, 0u, image);
        assert(planar_bios_reads == prior_bios_reads);
        assert(planar_bios_writes == prior_bios_writes);
    }
    assert(planar_vga_writes == 8u);
    assert(planar[18ul * 640u + 32u] == 1u);
    assert(planar[18ul * 640u + 33u] == 2u);
    assert(planar[18ul * 640u + 34u] == 4u);
    assert(planar[18ul * 640u + 35u] == 8u);
    assert(planar[19ul * 640u + 32u] == 10u);
    assert(planar[19ul * 640u + 33u] == 9u);
    assert(planar[19ul * 640u + 36u] == 2u);
    assert(gc_register[5] == 0u && gc_register[7] == 15u);
    assert(gc_register[8] == 255u && gc_register[3] == 0u);
    assert(seq_register[2] == 15u);
    gate_linked_blit_vfx_picture(0u, 18u, 32u, 3u, image);
    assert(planar[18ul * 640u + 32u] == 0u);
    assert(planar[19ul * 640u + 33u] == 0u);
    gate_linked_blit_vfx_picture(0u, 18u, 32u, 2u, image);
    assert(planar[18ul * 640u + 35u] == 8u);
    gate_linked_blit_vfx_picture(0u, 18u, 32u, 1u, image);
    assert(planar[19ul * 640u + 32u] == 10u);
    return 0;
}
''')
            binary = scratch / "raster-test"
            build = subprocess.run(
                [cc, "-std=c89", "-Wall", "-Wextra", "-Werror", "-pedantic",
                 "-I", str(scratch), "-I", str(staged), str(DOS_RASTER),
                 str(scratch / "check.c"), "-o", str(binary)],
                capture_output=True, text=True,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            tested = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(tested.returncode, 0, tested.stdout + tested.stderr)
            unsupported = subprocess.run([str(binary), "--unverified-buffer"],
                                         capture_output=True, text=True,
                                         cwd=str(scratch))
            self.assertEqual(unsupported.returncode, -signal.SIGABRT,
                             unsupported.stdout + unsupported.stderr)


if __name__ == "__main__":
    unittest.main()
