/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Direct host model of the DOS DCL file and segmented memory ABI. */
#include "GATEDOS.H"
#include "RTLIMAC.H"
#include <assert.h>
#include <string.h>
#include <stdio.h>

uint16_t gate_linked_decompress_picture_pixels(uint16_t offset, uint16_t segment);

static unsigned char ram[0x100000ul];
static const unsigned char input[] = {
    0x00,0x04,0x82,0x24,0x25,0x8f,0x80,0x7f,'N','E','X','T'
};
static long file_position;
static void *read_target;
static unsigned int mode = 3u;
static unsigned int width = 13u;
static unsigned int height = 1u;
static unsigned int read_calls;

static unsigned long address(uint16_t segment, uint16_t offset)
{
    return (((unsigned long)segment << 4) + offset) & 0xffffful;
}
uint8_t gate_far_byte(uint16_t segment, uint16_t offset)
{
    if (offset == GATE_RTL_VIDEO_MODE_OFFSET && segment == 0x1000u)
        return (uint8_t)mode;
    return ram[address(segment, offset)];
}
uint16_t gate_far_word(uint16_t segment, uint16_t offset)
{
    if (segment == 0x1000u) {
        if (offset == GATE_RTL_VIDEO_MODE_OFFSET) return (uint16_t)mode;
        if (offset == GATE_RTL_HEADER_OFFSET + 6u) return (uint16_t)width;
        if (offset == GATE_RTL_HEADER_OFFSET + 8u) return (uint16_t)height;
        if (offset == GATE_RTL_FILE_HANDLE_OFFSET) return 5u;
    }
    return (uint16_t)(ram[address(segment, offset)] |
                     ((uint16_t)ram[address(segment, (uint16_t)(offset + 1u))] << 8));
}
void gate_far_set_byte(uint16_t segment, uint16_t offset, uint8_t byte)
{
    ram[address(segment, offset)] = byte;
}
GateFarPtr gate_stack_pointer(void *buffer)
{
    GateFarPtr result;
    result.segment = 0xeeeeu;
    result.offset = 0xaaaau;
    read_target = buffer;
    return result;
}
GateDosFileResult gate_dos_file_read(uint16_t handle, GateFarPtr dest, uint16_t count)
{
    GateDosFileResult result;
    unsigned long available = sizeof input - (unsigned long)file_position;
    assert(handle == 5u && dest.segment == 0xeeeeu);
    if ((unsigned long)count > available) count = (uint16_t)available;
    memcpy(read_target, input + file_position, count);
    file_position += count;
    ++read_calls;
    result.ax = count;
    result.dx = 0u;
    result.carry = 0u;
    return result;
}
GateDosFileResult gate_dos_file_seek(uint16_t handle, uint16_t origin, uint32_t offset)
{
    GateDosFileResult result;
    assert(handle == 5u && origin == 1u);
    file_position += (int32_t)offset;
    assert(file_position >= 0 && file_position <= (long)sizeof input);
    result.ax = (uint16_t)file_position;
    result.dx = 0u;
    result.carry = 0u;
    return result;
}
uint16_t gate_rtl_image_mode_segment(void) { return 0x1000u; }
uint16_t gate_rtl_image_header_segment(void) { return 0x1000u; }
uint16_t gate_rtl_resource_handle_segment(void) { return 0x1000u; }

int main(void)
{
    unsigned int x, plane;
    unsigned int color;
    unsigned long base;
    uint16_t offset = 0xfff8u;
    uint16_t segment = 0x3500u;

    assert(gate_linked_decompress_picture_pixels(offset, segment) == 0u);
    assert(file_position == 8);
    assert(read_calls == 1u); /* byte read-ahead is buffered */
    for (x = 0; x < 13u; ++x) {
        base = (unsigned long)offset + x;
        assert(ram[address((uint16_t)(segment + ((base >> 16) << 12)),
                           (uint16_t)base)] == (unsigned char)(x & 1u ? 'I' : 'A'));
    }

    /* Every 4-bit color goes to four MSB-first planar rows, not chunky. */
    file_position = 0;
    mode = 2u;
    width = 26u;
    offset = 0x6200u;
    assert(gate_linked_decompress_picture_pixels(offset, segment) == 0u);
    assert(file_position == 8);
    for (x = 0; x < 26u; ++x) {
        color = 0u;
        for (plane = 0; plane < 4u; ++plane) {
            unsigned char bit = ram[address(segment,
                     (uint16_t)(offset + plane * 4u + x / 8u))];
            if (bit & (0x80u >> (x & 7u))) color |= (1u << plane);
        }
        assert(color == (x & 1u ? 4u : (x & 2u ? 9u : 1u)));
    }

    /* Zero-height and unsupported modes reject before touching the file. */
    height = 0u;
    assert(gate_linked_decompress_picture_pixels(offset, segment) == 0xffe6u);
    height = 1u;
    mode = 9u;
    assert(gate_linked_decompress_picture_pixels(offset, segment) == 0xfffau);
    mode = 3u;
    width = 20u;
    file_position = 0;
    assert(gate_linked_decompress_picture_pixels(offset, segment) == 0xfffau);
    assert(file_position == 8);
    puts("Host picture DCL: PASS");
    return 0;
}
