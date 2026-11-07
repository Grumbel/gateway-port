/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_rtl_video.h"

#include <string.h>

/* Stack buffer of the original calls is 14 bytes, addressed from BP-0x0e.
 * Only the bytes initialized by each compiled path are meaningful:
 * +1 command (2 move, 3 query, 9 character), +2 attribute, +3 subcommand,
 * +4 count, +6 column, +7 row, +0 character. The other stack bytes were
 * uninitialized; initializing them here avoids reading undefined host C.
 * Original RTL uses SS:buffer twice with a leading constant 0x10. */
static void video_call(uint8_t packet[GATE_VIDEO_PACKET_BYTES])
{
    gate_rtl_video_command(GATE_VIDEO_DISPATCH_FUNCTION, packet, packet);
}

/* GATE.RTL:0xf8eb..0xf920, emitted before/after a cursor operation in
 * block-cursor mode. It writes character 0xdb with attribute | 0x80. */
void gate_rtl_draw_block_cursor(void)
{
    uint8_t packet[GATE_VIDEO_PACKET_BYTES];
    memset(packet, 0, sizeof packet);
    packet[GATE_VIDEO_PACKET_COMMAND] = GATE_VIDEO_COMMAND_WRITE_CHARACTER;
    packet[GATE_VIDEO_PACKET_CHARACTER] = GATE_VIDEO_BLOCK_CURSOR_CHARACTER;
    packet[GATE_VIDEO_PACKET_SUBCOMMAND] = 0;
    packet[GATE_VIDEO_PACKET_ATTRIBUTE] = (uint8_t)(
        gate_rtl_video_attribute() | GATE_VIDEO_INVERT_ATTRIBUTE_BIT);
    packet[GATE_VIDEO_PACKET_COUNT_LOW] = 1;
    packet[GATE_VIDEO_PACKET_COUNT_HIGH] = 0;
    video_call(packet);
}

/* GATE.RTL:0xf921..0xf935, linked from the original intro as 0cdc:07d8.
 * Resident 2488:000a clears the display page with color zero. The RTL
 * cursor restoration is conditional on DS:030e. */
void gate_rtl_clear_video_display(void)
{
    gate_linked_clear_resident_display(0, 0);
    if (gate_rtl_video_block_mode() != 0)
        gate_rtl_draw_block_cursor();
}

/* GATE.RTL:0xf937..0xf958. */
uint16_t gate_rtl_set_block_cursor_mode(uint16_t new_mode)
{
    uint16_t previous = gate_rtl_video_block_mode();
    if (new_mode != previous)
        gate_rtl_draw_block_cursor();
    gate_rtl_video_set_block_mode(new_mode);
    return previous;
}

/* GATE.RTL:0xf959..0xf995: byte coordinates returned as 1-based WORDs.
 * Its FAR output pointers are unconditional (the original does not test
 * either for NULL). */
void gate_rtl_get_text_cell_cursor(uint16_t *x, uint16_t *y)
{
    uint8_t packet[GATE_VIDEO_PACKET_BYTES];
    memset(packet, 0, sizeof packet);
    packet[GATE_VIDEO_PACKET_COMMAND] = GATE_VIDEO_COMMAND_GET_CURSOR;
    packet[GATE_VIDEO_PACKET_SUBCOMMAND] = 0;
    video_call(packet);
    *y = (uint16_t)(packet[GATE_VIDEO_PACKET_ROW] + 1u);
    *x = (uint16_t)(packet[GATE_VIDEO_PACKET_COLUMN] + 1u);
}

/* GATE.RTL:0xf997..0xfa16: compare both coordinates AFTER 16-bit DEC.
 * Reading back byte coordinates before comparison is intentional: e.g.
 * requested x=0 wraps to 0xffff and cannot equal returned byte 0xff.
 * Nonzero DS:030e draws a block before and after a real change. */
void gate_rtl_set_text_cell_cursor(uint16_t x, uint16_t y)
{
    uint8_t packet[GATE_VIDEO_PACKET_BYTES];
    memset(packet, 0, sizeof packet);
    packet[GATE_VIDEO_PACKET_COMMAND] = GATE_VIDEO_COMMAND_GET_CURSOR;
    packet[GATE_VIDEO_PACKET_SUBCOMMAND] = 0;
    video_call(packet);
    x = (uint16_t)(x - 1u);
    y = (uint16_t)(y - 1u);
    if ((uint16_t)packet[GATE_VIDEO_PACKET_ROW] != y ||
        (uint16_t)packet[GATE_VIDEO_PACKET_COLUMN] != x) {
        if (gate_rtl_video_block_mode() != 0)
            gate_rtl_draw_block_cursor();
        packet[GATE_VIDEO_PACKET_COMMAND] = GATE_VIDEO_COMMAND_SET_CURSOR;
        packet[GATE_VIDEO_PACKET_SUBCOMMAND] = 0;
        packet[GATE_VIDEO_PACKET_ROW] = (uint8_t)y;
        packet[GATE_VIDEO_PACKET_COLUMN] = (uint8_t)x;
        video_call(packet);
        if (gate_rtl_video_block_mode() != 0)
            gate_rtl_draw_block_cursor();
    }
}

/* Original signed CWD/XOR/SUB/SAR/XOR/SUB expression; preserve INT16_MIN
 * and the low-16-bit add of four before shifting by three. */
static int16_t tab_pitch(void)
{
    uint16_t value = (uint16_t)((uint16_t)gate_rtl_video_tab_base() + GATE_TEXT_TAB_PITCH_BIAS_PIXELS);
    int negative = (value & GATE_TEXT_SIGNED_WORD_SIGN_BIT) != 0;
    uint16_t magnitude = negative ? (uint16_t)(-value) : value;
    uint16_t shifted = (uint16_t)((magnitude >> GATE_TEXT_CELL_WIDTH_SHIFT) |
                                   ((magnitude & GATE_TEXT_SIGNED_WORD_SIGN_BIT) ? GATE_TEXT_CELL_SAR_SIGN_FILL : 0));
    return (int16_t)(negative ? (uint16_t)(-shifted) : shifted);
}

/* GATE.RTL:0xfa45..0xfbf3. String pointer is normalized by EXE:0x0bb8
 * (identity). The printer is bounded by signed row/column comparisons,
 * and sends cursor commands before glyphs, for tabs and for newlines.
 * Critically, it increments its local column even after TAB/newline; the
 * command packet itself may have sent a different 8-bit column. */
void gate_rtl_print_text_mode_string(GateFarPtr text)
{
    uint8_t packet[GATE_VIDEO_PACKET_BYTES];
    int16_t row, column;
    uint8_t attribute;

    memset(packet, 0, sizeof packet);

    text = gate_exe_identity_text_pointer(text);
    if (gate_rtl_video_block_mode() != 0)
        gate_rtl_draw_block_cursor();

    packet[GATE_VIDEO_PACKET_COMMAND] = GATE_VIDEO_COMMAND_GET_CURSOR;
    packet[GATE_VIDEO_PACKET_SUBCOMMAND] = 0;
    video_call(packet);
    row = (int16_t)(uint16_t)packet[GATE_VIDEO_PACKET_ROW];
    column = (int16_t)(uint16_t)packet[GATE_VIDEO_PACKET_COLUMN];
    if (row >= 0 && row < gate_rtl_video_rows()) {
        if (gate_rtl_video_inversion() != 0) {
            attribute = (uint8_t)((gate_rtl_video_attribute() ^
                                   (uint8_t)gate_rtl_video_inversion()) |
                                  GATE_VIDEO_INVERT_ATTRIBUTE_BIT);
        } else {
            attribute = gate_rtl_video_attribute();
        }

        while (gate_far_byte(text.segment, text.offset) != 0) {
            uint8_t ch = gate_far_byte(text.segment, text.offset);
            if (ch == GATE_TEXT_CHARACTER_TAB) {
                int16_t pitch = tab_pitch();
                int16_t quotient = (int16_t)(column / pitch);
                column = (int16_t)(uint16_t)((uint16_t)(quotient * (int32_t)pitch) +
                                              (uint16_t)pitch);
                packet[GATE_VIDEO_PACKET_COMMAND] = GATE_VIDEO_COMMAND_SET_CURSOR;
                packet[GATE_VIDEO_PACKET_SUBCOMMAND] = 0;
                packet[GATE_VIDEO_PACKET_ROW] = (uint8_t)row;
                packet[GATE_VIDEO_PACKET_COLUMN] = (uint8_t)column;
                video_call(packet);
            } else if (ch == GATE_TEXT_CHARACTER_NEWLINE) {
                column = 0;
                row = (int16_t)(uint16_t)((uint16_t)row + 1u);
                packet[GATE_VIDEO_PACKET_COMMAND] = GATE_VIDEO_COMMAND_SET_CURSOR;
                packet[GATE_VIDEO_PACKET_SUBCOMMAND] = 0;
                packet[GATE_VIDEO_PACKET_ROW] = (uint8_t)row;
                packet[GATE_VIDEO_PACKET_COLUMN] = 0;
                video_call(packet);
            } else if (column >= 0 && column < gate_rtl_video_columns()) {
                packet[GATE_VIDEO_PACKET_COMMAND] = GATE_VIDEO_COMMAND_SET_CURSOR;
                packet[GATE_VIDEO_PACKET_SUBCOMMAND] = 0;
                packet[GATE_VIDEO_PACKET_ROW] = (uint8_t)row;
                packet[GATE_VIDEO_PACKET_COLUMN] = (uint8_t)column;
                video_call(packet);
                packet[GATE_VIDEO_PACKET_COMMAND] = GATE_VIDEO_COMMAND_WRITE_CHARACTER;
                packet[GATE_VIDEO_PACKET_CHARACTER] = ch;
                packet[GATE_VIDEO_PACKET_SUBCOMMAND] = 0;
                packet[GATE_VIDEO_PACKET_ATTRIBUTE] = attribute;
                packet[GATE_VIDEO_PACKET_COUNT_LOW] = 1;
                packet[GATE_VIDEO_PACKET_COUNT_HIGH] = 0;
                video_call(packet);
            }

            column = (int16_t)(uint16_t)((uint16_t)column + 1u);
            text.offset = (uint16_t)(text.offset + 1u);
        }
    }

    packet[GATE_VIDEO_PACKET_COMMAND] = GATE_VIDEO_COMMAND_SET_CURSOR;
    packet[GATE_VIDEO_PACKET_SUBCOMMAND] = 0;
    packet[GATE_VIDEO_PACKET_ROW] = (uint8_t)row;
    packet[GATE_VIDEO_PACKET_COLUMN] = (uint8_t)column;
    video_call(packet);
    if (gate_rtl_video_block_mode() != 0)
        gate_rtl_draw_block_cursor();
}
