/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Read and write genuine guest DS fields; query BIOS mode under DOSBox. */
#include "GATEMEM.H"
#include "RTLLAY.H"
#include <dos.h>
#include <stdio.h>

void gate_rtl_resident_set_pen_color(uint16_t color);
void gate_rtl_resident_move_to(uint16_t x, uint16_t y);
void gate_rtl_background_setup(uint16_t pattern, uint16_t color, uint16_t transparent);
uint16_t gate_rtl_mode_set_bios(uint16_t mode);
uint16_t gate_rtl_mode_start(uint16_t enabled);
uint16_t gate_linked_clear_resident_display(uint16_t page, uint16_t color);
void gate_rtl_plot_pixel(uint16_t color, uint16_t y, uint16_t x);
void gate_rtl_resident_line_to(uint16_t x, uint16_t y);
void gate_rtl_background_rect(uint16_t style, uint16_t bottom,
                              uint16_t right, uint16_t top, uint16_t left);
void gate_linked_blit_vfx_picture(uint16_t zero, uint16_t y, uint16_t x,
                                  uint16_t operation, GateFarPtr image);

static int check(uint16_t actual, uint16_t expected, const char *label)
{
    if (actual != expected) {
        printf("Resident state probe: %s got %04x expected %04x\n",
               label, actual, expected);
        return 0;
    }
    return 1;
}

int main(void)
{
    unsigned int segment = 0;
    int valid = 1;
    union REGS in, out;
    uint8_t original_bios_mode;
    GateFarPtr image;

    if (_dos_allocmem(0x1000u, &segment) != 0) {
        puts("Resident state probe: DOS memory allocation failed");
        return 1;
    }
    gate_dos_bind_ds((uint16_t)segment);
    gate_ds_set_word(GATE_DS_RENDERER_MODE, 8u);
    valid &= check(gate_linked_clear_resident_display(0u, 0u), 0xfffau,
                   "clear rejects invalid mode");
    gate_ds_set_word(GATE_DS_RENDERER_MODE, 0u);
    gate_far_set_byte((uint16_t)segment, GATE_RESIDENT_MODE_DESCRIPTOR_BASE, 3u);
    valid &= check(gate_linked_clear_resident_display(0u, 0u), 0xfc19u,
                   "clear rejects mismatched descriptor");
    gate_rtl_resident_set_pen_color(0xabcdu);
    valid &= check(gate_ds_word(GATE_DS_DRAW_COLOR), 0xabcdu, "pen color");
    gate_rtl_resident_move_to(300u, 99u);
    valid &= check(gate_ds_word(GATE_DS_DRAW_X), 300u, "pen x");
    valid &= check(gate_ds_word(GATE_DS_DRAW_Y), 99u, "pen y");
    gate_rtl_background_setup(11u, 15u, 1u);
    valid &= check(gate_ds_word(GATE_DS_DRAW_FILL_PATTERN), 11u, "fill pattern");
    valid &= check(gate_ds_word(GATE_DS_DRAW_FILL_COLOR), 15u, "fill color");
    valid &= check(gate_ds_word(GATE_DS_DRAW_FILL_TRANSPARENT), 1u, "fill alpha");
    gate_rtl_background_setup(0u, 4u, 2u);
    valid &= check(gate_ds_word(GATE_DS_DRAW_FILL_TRANSPARENT), 0u,
                   "non-one transparency disabled");

    valid &= check(gate_rtl_mode_set_bios(8u), 0xfffau, "reject invalid mode");
    valid &= check(gate_rtl_mode_set_bios(0u), 0u, "accept text descriptor");
    valid &= check(gate_ds_word(GATE_DS_RENDERER_MODE), 0u, "descriptor selected");
    valid &= check(gate_rtl_mode_start(2u), 0xfff9u, "reject invalid enable");
    in.h.ah = 0x0fu;
    int86(0x10, &in, &out);
    original_bios_mode = out.h.al;
    gate_far_set_byte((uint16_t)segment,
        (uint16_t)(GATE_RESIDENT_MODE_DESCRIPTOR_BASE + GATE_RESIDENT_MODE_BIOS_NUMBER),
        original_bios_mode);
    valid &= check(gate_rtl_mode_start(1u), 0u, "start with current BIOS mode");
    valid &= check(gate_ds_word(GATE_DS_RENDERER_ENABLED), 1u, "video enabled");
    valid &= check(gate_ds_word(GATE_DS_RENDERER_STATUS), 0u, "video status cleared");
    in.h.ah = 0x0fu;
    int86(0x10, &in, &out);
    valid &= check(out.h.al, original_bios_mode, "BIOS mode unchanged");

    /* The mode-13 path writes genuine A000 bytes. The original 25d7
     * dispatch mode is set independently of the BIOS mode, so set both. */
    in.x.ax = 0x0013u;
    int86(0x10, &in, &out);
    gate_ds_set_word(GATE_DS_RENDERER_MODE, 3u);
    gate_ds_set_word(GATE_DS_DRAW_CLIPPING_ENABLED, 0u);
    gate_ds_set_word(GATE_DS_DRAW_OPERATION, 0u);
    gate_rtl_plot_pixel(0x42u, 4u, 9u);
    valid &= check(gate_far_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT,
                                 (uint16_t)(4u * 320u + 9u)),
                   0x42u, "mode-13 pixel copy");
    gate_ds_set_word(GATE_DS_DRAW_OPERATION, 3u);
    gate_rtl_plot_pixel(0x0fu, 4u, 9u);
    valid &= check(gate_far_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT,
                                 (uint16_t)(4u * 320u + 9u)),
                   0x4du, "mode-13 pixel XOR");
    gate_ds_set_word(GATE_DS_DRAW_OPERATION, 0u);
    gate_ds_set_word(GATE_DS_DRAW_LINE_WIDTH, 1u);
    gate_ds_set_word(GATE_DS_DRAW_LINE_MASK, 0xffffu);
    gate_rtl_resident_set_pen_color(0x0au);
    gate_rtl_resident_move_to(12u, 12u);
    gate_rtl_resident_line_to(15u, 12u);
    valid &= check(gate_far_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT,
                                 (uint16_t)(12u * 320u + 13u)),
                   0x0au, "horizontal line interior");
    valid &= check(gate_ds_word(GATE_DS_DRAW_X), 15u, "line updates pen X");
    gate_rtl_background_setup(0u, 0x20u, 0u);
    gate_rtl_background_rect(2u, 18u, 18u, 16u, 16u);
    valid &= check(gate_far_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT,
                                 (uint16_t)(17u * 320u + 17u)),
                   0x20u, "solid rectangle interior");
    valid &= check(gate_ds_word(GATE_DS_DRAW_COLOR), 0x0au,
                   "rectangle restores pen color");
    valid &= check(gate_ds_word(GATE_DS_DRAW_LINE_MASK), 0xffffu,
                   "rectangle restores line mask");
    gate_rtl_background_rect(1u, 22u, 22u, 20u, 20u);
    valid &= check(gate_far_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT,
                                 (uint16_t)(20u * 320u + 21u)),
                   0x0au, "outline top edge");
    valid &= check(gate_ds_word(GATE_DS_DRAW_X), 15u,
                   "outline restores pen X");
    valid &= check(gate_ds_word(GATE_DS_DRAW_Y), 12u,
                   "outline restores pen Y");

    image.segment = (uint16_t)segment;
    image.offset = 0x0500u;
    gate_far_set_word(image.segment, image.offset, 0xca00u);
    gate_far_set_word(image.segment, image.offset + 0x2cu, 2u);
    gate_far_set_word(image.segment, image.offset + 0x2eu, 1u);
    gate_far_set_word(image.segment, image.offset + 0x30u, 2u);
    gate_far_set_word(image.segment, image.offset + 0x14u, 2u);
    gate_far_set_word(image.segment, image.offset + 0x18u, 0x0800u);
    gate_far_set_word(image.segment, image.offset + 0x1au, (uint16_t)segment);
    gate_far_set_byte(image.segment, image.offset + 0x12u, 1u);
    gate_far_set_byte(image.segment, image.offset + 0x13u, 8u);
    gate_far_set_byte(image.segment, 0x0800u, 0x25u);
    gate_far_set_byte(image.segment, 0x0801u, 0x4au);
    gate_linked_blit_vfx_picture(0u, 30u, 31u, 0u, image);
    valid &= check(gate_far_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT,
                                 (uint16_t)(30u * 320u + 31u)),
                   0x25u, "mode-13 VFX pixel copy");
    valid &= check(gate_far_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT,
                                 (uint16_t)(30u * 320u + 32u)),
                   0x4au, "mode-13 VFX pixel adjacent");
    gate_linked_blit_vfx_picture(0u, 30u, 31u, 3u, image);
    valid &= check(gate_far_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT,
                                 (uint16_t)(30u * 320u + 31u)),
                   0u, "mode-13 VFX XOR restore");

    /* The 640x480 branch uses real BIOS planar pixels rather than treating
     * A000 as a linear 4-bit buffer. Test only one pixel to avoid replacing
     * the original resident rasterizer's fast path with a slow bulk test. */
    in.x.ax = 0x0012u;
    int86(0x10, &in, &out);
    gate_ds_set_word(GATE_DS_RENDERER_MODE, 2u);
    gate_rtl_plot_pixel(0x0bu, 4u, 5u);
    in.x.ax = 0x0d00u;
    in.x.bx = 0u;
    in.x.cx = 5u;
    in.x.dx = 4u;
    int86(0x10, &in, &out);
    valid &= check((uint16_t)(out.h.al & 15u), 0x0bu, "planar VGA pixel");

    gate_far_set_word(image.segment, image.offset + 0x14u, 1u);
    gate_far_set_byte(image.segment, image.offset + 0x12u, 4u);
    gate_far_set_byte(image.segment, image.offset + 0x13u, 1u);
    gate_far_set_byte(image.segment, 0x0800u, 0x80u);
    gate_far_set_byte(image.segment, 0x0801u, 0x40u);
    gate_far_set_byte(image.segment, 0x0802u, 0x80u);
    gate_far_set_byte(image.segment, 0x0803u, 0u);
    gate_linked_blit_vfx_picture(0u, 15u, 16u, 0u, image);
    in.x.ax = 0x0d00u;
    in.x.bx = 0u;
    in.x.cx = 16u;
    in.x.dx = 15u;
    int86(0x10, &in, &out);
    valid &= check((uint16_t)(out.h.al & 15u), 5u,
                   "mode-12 VFX four-plane pixel");
    in.x.cx = 17u;
    int86(0x10, &in, &out);
    valid &= check((uint16_t)(out.h.al & 15u), 2u,
                   "mode-12 VFX alternate pixel");

    in.x.ax = original_bios_mode;
    int86(0x10, &in, &out);

    if (_dos_freemem(segment) != 0) {
        puts("Resident state probe: DOS memory free failed");
        return 1;
    }
    puts(valid ? "Resident state probe: PASS" : "Resident state probe: FAIL");
    return valid ? 0 : 1;
}
