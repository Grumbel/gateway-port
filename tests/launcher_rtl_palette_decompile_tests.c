/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "gate_rtl_palette.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { RAM_SEG = 0x4321, STATUS_PORT = 0x3da };
static uint8_t ram[65536];
static uint16_t mode, video_type, conversion, count_index;
static int16_t color_count;
static uint8_t input_values[32];
static unsigned input_length, input_next, far_reads, table_reads;

struct Operation { char kind; uint16_t port; uint8_t value; };
static struct Operation operations[1024];
static unsigned op_count;

uint16_t gate_rtl_palette_mode(void) { return mode; }
uint16_t gate_rtl_palette_video_type(void) { return video_type; }
uint16_t gate_rtl_palette_status_port(void) { return STATUS_PORT; }
int16_t gate_rtl_palette_color_count(uint16_t offset)
{
    count_index = offset;
    return color_count;
}
uint16_t gate_rtl_palette_conversion(void) { return conversion; }
uint8_t gate_rtl_palette_table_byte(uint16_t offset)
{
    ++table_reads;
    return (uint8_t)((offset & 0xffu) ^ 0x53u);
}
uint8_t gate_far_byte(uint16_t segment, uint16_t offset)
{
    ++far_reads;
    if (segment == 0 && offset == 0)
        return 0;
    assert(segment == RAM_SEG);
    return ram[offset];
}
uint8_t gate_rtl_palette_in(uint16_t port)
{
    struct Operation *operation;
    assert(port == STATUS_PORT);
    assert(input_next < input_length);
    assert(op_count < sizeof operations / sizeof operations[0]);
    operation = &operations[op_count++];
    operation->kind = 'I';
    operation->port = port;
    operation->value = input_values[input_next++];
    return operation->value;
}
void gate_rtl_palette_out(uint16_t port, uint8_t value)
{
    struct Operation *operation;
    assert(port == 0x3c0 || port == 0x3c8 || port == 0x3c9);
    assert(op_count < sizeof operations / sizeof operations[0]);
    operation = &operations[op_count++];
    operation->kind = 'O';
    operation->port = port;
    operation->value = value;
}

static void reset(uint16_t new_mode, uint16_t type,
                  int16_t count, uint16_t convert)
{
    unsigned i;
    mode = new_mode;
    video_type = type;
    color_count = count;
    conversion = convert;
    count_index = 0;
    input_length = input_next = 0;
    op_count = far_reads = table_reads = 0;
    for (i = 0; i < sizeof ram; ++i)
        ram[i] = (uint8_t)(i * 29u + 3u);
    memset(operations, 0, sizeof operations);
}

static void status(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    input_values[0] = a;
    input_values[1] = b;
    input_values[2] = c;
    input_values[3] = d;
    input_length = 4;
}

static void output_at(unsigned index, uint16_t port, uint8_t value)
{
    assert(index < op_count);
    assert(operations[index].kind == 'O');
    assert(operations[index].port == port);
    assert(operations[index].value == value);
}

static void test_retrace_and_disabled_mode(void)
{
    reset(0, 5, 48, 0);
    gate_rtl_f61b((GateFarPtr){0, 0});
    assert(input_next == 0 && op_count == 0 && far_reads == 0);

    reset(1, 5, 16, 0);
    status(8, 8, 0, 8);
    gate_rtl_f473();
    assert(input_next == 4 && op_count == 4);
    assert(operations[0].kind == 'I' && operations[3].kind == 'I');
}

static void test_direct_dac_and_attribute_tail(void)
{
    unsigned i;
    GateFarPtr ptr = {RAM_SEG, 0xfffd};
    reset(3, 5, 9, 0); /* 9 component bytes = 3 DAC palette entries */
    status(0, 8, 0, 0);
    gate_rtl_f61b(ptr);
    assert(input_next == 2 && count_index == 10);
    assert(op_count == 2 + 1 + 9 && far_reads == 10);
    output_at(2, 0x3c8, 0);
    for (i = 0; i < 9; ++i)
        output_at(3 + i, 0x3c9,
                  ram[(uint16_t)(ptr.offset + (uint16_t)i)]);

    reset(2, 5, 6, 0);
    status(0, 8, 0x11, 0);
    ram[0x100] = 0; /* first byte also decides attribute register */
    gate_rtl_f61b((GateFarPtr){RAM_SEG, 0x100});
    assert(input_next == 3 && count_index == 8);
    assert(op_count == 3 + 1 + 6 + 3);
    output_at(op_count - 3, 0x3c0, 0x11);
    output_at(op_count - 2, 0x3c0, 0x00);
    output_at(op_count - 1, 0x3c0, 0x20);
}

static void test_mode_one_packed_colors(void)
{
    reset(1, 5, 2, 0);
    status(0, 8, 0, 0);
    ram[0x101] = 0x33;
    ram[0x102] = 0x5e;
    gate_rtl_f61b((GateFarPtr){RAM_SEG, 0x101});
    assert(input_next == 3 && far_reads == 3 && count_index == 6);
    assert(op_count == 3 + 1 + 6 + 3);
    output_at(3, 0x3c9, 21 * (((0x33 >> 1) & 2) | ((0x33 >> 5) & 1)));
    output_at(4, 0x3c9, 21 * (((0x33 >> 4) & 1) | (0x33 & 2)));
    output_at(5, 0x3c9, 21 * (((0x33 << 1) & 2) | ((0x33 >> 3) & 1)));
    output_at(6, 0x3c9, 21 * (((0x5e >> 1) & 2) | ((0x5e >> 5) & 1)));
    output_at(op_count - 2, 0x3c0, 15);
}

static void test_converted_vga_modes(void)
{
    GateFarPtr ptr = {RAM_SEG, 0xfffd};
    uint16_t table0, table1;
    reset(2, 5, 6, 1);
    status(0, 8, 0, 0);
    ram[0xfffd] = 0x80; /* signed -128 / 31 = -4; index -12 */
    ram[0x0000] = 0x3e; /* +62 / 31 = +2; index +6 */
    gate_rtl_f61b(ptr);
    assert(input_next == 3 && far_reads == 3 && table_reads == 4);
    table0 = (uint16_t)(0x35c - 12);
    table1 = (uint16_t)(0x366 - 12);
    output_at(3, 0x3c9, (uint8_t)((table0 & 0xffu) ^ 0x53));
    output_at(4, 0x3c9, (uint8_t)((table1 & 0xffu) ^ 0x53));
    output_at(5, 0x3c9, 0);
    output_at(6, 0x3c9, (uint8_t)(((0x35c + 6) & 0xffu) ^ 0x53));

    reset(1, 5, 1, 1);
    status(0, 8, 0, 0);
    ram[0x200] = 0x3f;
    gate_rtl_f61b((GateFarPtr){RAM_SEG, 0x200});
    /* Bits 0..2 contribute 2 each, bits 3..5 contribute 1 each. */
    output_at(3, 0x3c9, (uint8_t)(((0x35c + 9) & 0xffu) ^ 0x53));
    output_at(4, 0x3c9, (uint8_t)(((0x366 + 9) & 0xffu) ^ 0x53));
    assert(far_reads == 2 && table_reads == 2);
}

static void test_attribute_only_adapter(void)
{
    unsigned i;
    GateFarPtr ptr = {RAM_SEG, 0xfffc};
    reset(2, 4, 48, 0);
    status(0, 8, 0, 0);
    gate_rtl_f61b(ptr);
    assert(input_next == 4 && far_reads == 16);
    assert(op_count == 4 + 32 + 3);
    for (i = 0; i < 16; ++i) {
        output_at(3 + 2*i, 0x3c0, (uint8_t)i);
        output_at(4 + 2*i, 0x3c0,
                  ram[(uint16_t)(ptr.offset + (uint16_t)i)]);
    }
    output_at(op_count - 3, 0x3c0, 0x11);
    output_at(op_count - 2, 0x3c0, 0);
    output_at(op_count - 1, 0x3c0, 0x20);
}

int main(void)
{
    test_retrace_and_disabled_mode();
    test_direct_dac_and_attribute_tail();
    test_mode_one_packed_colors();
    test_converted_vga_modes();
    test_attribute_only_adapter();
    puts("original RTL retrace and palette/VGA output: OK");
    return 0;
}
