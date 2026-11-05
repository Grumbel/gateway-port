/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "gate_rtl_video.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { TEXT_SEG = 0x4444 };
static uint8_t text[65536];
static uint16_t mode, invert;
static uint8_t ink, cursor_x, cursor_y;
static int16_t columns, rows, tab_base;

struct Command { uint8_t op, ch, attr, row, col; uint16_t length; };
static struct Command commands[80];
static unsigned count;

uint8_t gate_far_byte(uint16_t segment, uint16_t offset)
{
    assert(segment == TEXT_SEG);
    return text[offset];
}

uint16_t gate_rtl_video_block_mode(void) { return mode; }
void gate_rtl_video_set_block_mode(uint16_t value) { mode = value; }
uint16_t gate_rtl_video_inversion(void) { return invert; }
uint8_t gate_rtl_video_attribute(void) { return ink; }
int16_t gate_rtl_video_columns(void) { return columns; }
int16_t gate_rtl_video_rows(void) { return rows; }
int16_t gate_rtl_video_tab_base(void) { return tab_base; }

void gate_rtl_video_command(uint16_t function, uint8_t *input, uint8_t *output)
{
    struct Command *command;
    assert(function == 0x10 && input == output);
    assert(count < sizeof commands / sizeof commands[0]);
    command = &commands[count++];
    command->op = input[1];
    command->ch = input[0];
    command->attr = input[2];
    command->row = input[7];
    command->col = input[6];
    command->length = (uint16_t)(input[4] | ((uint16_t)input[5] << 8));
    switch (input[1]) {
    case 2:
        cursor_x = input[6];
        cursor_y = input[7];
        break;
    case 3:
        output[6] = cursor_x;
        output[7] = cursor_y;
        break;
    case 9:
        break;
    default:
        assert(!"unrecognized original video command");
    }
}

static void reset(void)
{
    memset(text, 0, sizeof text);
    memset(commands, 0, sizeof commands);
    count = 0;
    mode = invert = cursor_x = cursor_y = 0;
    ink = 7;
    columns = 80;
    rows = 25;
    tab_base = 60; /* original ADD 4 / SAR 3 => 8-cell tabs */
}

static void test_cursor_queries_and_mode_changes(void)
{
    uint16_t x = 0, y = 0;
    reset();
    cursor_x = 255;
    cursor_y = 254;
    gate_rtl_f959(&x, &y);
    assert(x == 256 && y == 255 && count == 1 && commands[0].op == 3);

    reset();
    gate_rtl_f997(1, 1);
    assert(count == 1 && commands[0].op == 3);
    gate_rtl_f997(3, 4);
    assert(count == 3 && commands[1].op == 3 && commands[2].op == 2);
    assert(cursor_x == 2 && cursor_y == 3);

    /* DS:030e transition calls RTL:f8eb even when turning the mode off. */
    assert(gate_rtl_f937(1) == 0 && mode == 1);
    assert(count == 4 && commands[3].op == 9 && commands[3].ch == 0xdb);
    assert(commands[3].attr == 0x87 && commands[3].length == 1);
    gate_rtl_f997(4, 6);
    assert(count == 8);
    assert(commands[4].op == 3 && commands[5].op == 9 &&
           commands[6].op == 2 && commands[7].op == 9);
    assert(cursor_x == 3 && cursor_y == 5);
    assert(gate_rtl_f937(0) == 1 && mode == 0);
    assert(count == 9 && commands[8].op == 9);
}

static void test_original_text_control_flow(void)
{
    const GateFarPtr ptr = { TEXT_SEG, 0x0100 };
    const struct Command *c;
    reset();
    memcpy(text + 0x0100, "A\tB\nC", 6);
    ink = 0x0b;
    invert = 4;
    gate_rtl_fa45(ptr);
    assert(count == 10);
    assert(commands[0].op == 3);
    assert(commands[1].op == 2 && commands[1].row == 0 && commands[1].col == 0);
    c = &commands[2];
    assert(c->op == 9 && c->ch == 'A' && c->attr == 0x8f && c->length == 1);
    assert(commands[3].op == 2 && commands[3].col == 8);
    assert(commands[4].op == 2 && commands[4].col == 9);
    assert(commands[5].op == 9 && commands[5].ch == 'B');
    assert(commands[6].op == 2 && commands[6].row == 1 && commands[6].col == 0);
    /* The original increments its local column AFTER newline; it doesn't
     * start the next printable character at that dispatched column zero. */
    assert(commands[7].op == 2 && commands[7].row == 1 && commands[7].col == 1);
    assert(commands[8].op == 9 && commands[8].ch == 'C');
    assert(commands[9].op == 2 && commands[9].row == 1 && commands[9].col == 2);
    assert(cursor_x == 2 && cursor_y == 1);

    reset();
    text[0xffff] = 'Q';
    text[0] = 0x80;
    text[1] = 0;
    gate_rtl_fa45((GateFarPtr){TEXT_SEG, 0xffff});
    assert(count == 6 && commands[2].ch == 'Q' && commands[4].ch == 0x80);
    assert(commands[5].op == 2 && commands[5].col == 2);
}

static void test_clipping_and_cursor_wrap(void)
{
    const GateFarPtr ptr = { TEXT_SEG, 0 };
    reset();
    cursor_y = 25;
    text[0] = 'X';
    gate_rtl_fa45(ptr);
    assert(count == 2 && commands[0].op == 3 && commands[1].op == 2);
    assert(commands[1].row == 25); /* no glyph above row limit */

    reset();
    cursor_x = 2;
    columns = 2;
    text[0] = 'X';
    text[1] = 0;
    gate_rtl_fa45(ptr);
    assert(count == 2 && commands[1].col == 3);

    reset();
    gate_rtl_f997(0, 0);
    /* DEC of 1-based arguments happens in WORDs, while the packet holds
     * bytes; 0 becomes 0xffff and is transmitted as 0xff. */
    assert(count == 2 && commands[1].col == 255 && commands[1].row == 255);
}

static void test_print_with_block_cursor(void)
{
    const GateFarPtr ptr = { TEXT_SEG, 0x0100 };
    reset();
    mode = 1;
    ink = 5;
    text[0x0100] = 'X';
    gate_rtl_fa45(ptr);
    assert(count == 6);
    assert(commands[0].op == 9 && commands[0].ch == 0xdb &&
           commands[0].attr == 0x85);
    assert(commands[1].op == 3);
    assert(commands[2].op == 2 && commands[3].op == 9);
    assert(commands[3].ch == 'X' && commands[3].attr == 5);
    assert(commands[4].op == 2 && commands[4].col == 1);
    assert(commands[5].op == 9 && commands[5].ch == 0xdb);
}

int main(void)
{
    test_cursor_queries_and_mode_changes();
    test_original_text_control_flow();
    test_clipping_and_cursor_wrap();
    test_print_with_block_cursor();
    puts("original RTL video commands: cursor, block mode, print, tab, newline passed");
    return 0;
}
