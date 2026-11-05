/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_rtl_video.h"

/* Stack buffer of the original calls is 14 bytes, addressed from BP-0x0e.
 * Only the bytes initialized by each compiled path are meaningful:
 * +1 command (2 move, 3 query, 9 character), +2 attribute, +3 subcommand,
 * +4 count, +6 column, +7 row, +0 character. The other stack bytes were
 * uninitialized; initializing them here avoids reading undefined host C.
 * Original RTL uses SS:buffer twice with a leading constant 0x10. */
static void video_call(uint8_t packet[14])
{
    gate_rtl_video_command(0x10, packet, packet);
}

/* GATE.RTL:0xf8eb..0xf920, emitted before/after a cursor operation in
 * block-cursor mode. It writes character 0xdb with attribute | 0x80. */
void gate_rtl_f8eb(void)
{
    uint8_t packet[14] = {0};
    packet[1] = 9;
    packet[0] = 0xdb;
    packet[3] = 0;
    packet[2] = (uint8_t)(gate_rtl_video_attribute() | 0x80u);
    packet[4] = 1;
    packet[5] = 0;
    video_call(packet);
}

/* GATE.RTL:0xf937..0xf958. The preceding 0xf921 wrapper is separate. */
uint16_t gate_rtl_f937(uint16_t new_mode)
{
    uint16_t previous = gate_rtl_video_block_mode();
    if (new_mode != previous)
        gate_rtl_f8eb();
    gate_rtl_video_set_block_mode(new_mode);
    return previous;
}

/* GATE.RTL:0xf959..0xf995: byte coordinates returned as 1-based WORDs.
 * Its FAR output pointers are unconditional (the original does not test
 * either for NULL). */
void gate_rtl_f959(uint16_t *x, uint16_t *y)
{
    uint8_t packet[14] = {0};
    packet[1] = 3;
    packet[3] = 0;
    video_call(packet);
    *y = (uint16_t)(packet[7] + 1u);
    *x = (uint16_t)(packet[6] + 1u);
}

/* GATE.RTL:0xf997..0xfa16: compare both coordinates AFTER 16-bit DEC.
 * Reading back byte coordinates before comparison is intentional: e.g.
 * requested x=0 wraps to 0xffff and cannot equal returned byte 0xff.
 * Nonzero DS:030e draws a block before and after a real change. */
void gate_rtl_f997(uint16_t x, uint16_t y)
{
    uint8_t packet[14] = {0};
    packet[1] = 3;
    packet[3] = 0;
    video_call(packet);
    x = (uint16_t)(x - 1u);
    y = (uint16_t)(y - 1u);
    if ((uint16_t)packet[7] != y || (uint16_t)packet[6] != x) {
        if (gate_rtl_video_block_mode() != 0)
            gate_rtl_f8eb();
        packet[1] = 2;
        packet[3] = 0;
        packet[7] = (uint8_t)y;
        packet[6] = (uint8_t)x;
        video_call(packet);
        if (gate_rtl_video_block_mode() != 0)
            gate_rtl_f8eb();
    }
}

/* Original signed CWD/XOR/SUB/SAR/XOR/SUB expression; preserve INT16_MIN
 * and the low-16-bit add of four before shifting by three. */
static int16_t tab_pitch(void)
{
    uint16_t value = (uint16_t)((uint16_t)gate_rtl_video_tab_base() + 4u);
    int negative = (value & 0x8000u) != 0;
    uint16_t magnitude = negative ? (uint16_t)(-value) : value;
    uint16_t shifted = (uint16_t)((magnitude >> 3) |
                                   ((magnitude & 0x8000u) ? 0xe000u : 0));
    return (int16_t)(negative ? (uint16_t)(-shifted) : shifted);
}

/* GATE.RTL:0xfa45..0xfbf3. String pointer is normalized by EXE:0x0bb8
 * (identity). The printer is bounded by signed row/column comparisons,
 * and sends cursor commands before glyphs, for tabs and for newlines.
 * Critically, it increments its local column even after TAB/newline; the
 * command packet itself may have sent a different 8-bit column. */
void gate_rtl_fa45(GateFarPtr text)
{
    uint8_t packet[14] = {0};
    int16_t row, column;
    uint8_t attribute;

    text = gate_exe_0bb8(text);
    if (gate_rtl_video_block_mode() != 0)
        gate_rtl_f8eb();

    packet[1] = 3;
    packet[3] = 0;
    video_call(packet);
    row = (int16_t)(uint16_t)packet[7];
    column = (int16_t)(uint16_t)packet[6];
    if (row >= 0 && row < gate_rtl_video_rows()) {
        if (gate_rtl_video_inversion() != 0) {
            attribute = (uint8_t)((gate_rtl_video_attribute() ^
                                   (uint8_t)gate_rtl_video_inversion()) | 0x80u);
        } else {
            attribute = gate_rtl_video_attribute();
        }

        while (gate_far_byte(text.segment, text.offset) != 0) {
            uint8_t ch = gate_far_byte(text.segment, text.offset);
            if (ch == 9) {
                int16_t pitch = tab_pitch();
                int16_t quotient = (int16_t)(column / pitch);
                column = (int16_t)(uint16_t)((uint16_t)(quotient * (int32_t)pitch) +
                                              (uint16_t)pitch);
                packet[1] = 2;
                packet[3] = 0;
                packet[7] = (uint8_t)row;
                packet[6] = (uint8_t)column;
                video_call(packet);
            } else if (ch == 10) {
                column = 0;
                row = (int16_t)(uint16_t)((uint16_t)row + 1u);
                packet[1] = 2;
                packet[3] = 0;
                packet[7] = (uint8_t)row;
                packet[6] = 0;
                video_call(packet);
            } else if (column >= 0 && column < gate_rtl_video_columns()) {
                packet[1] = 2;
                packet[3] = 0;
                packet[7] = (uint8_t)row;
                packet[6] = (uint8_t)column;
                video_call(packet);
                packet[1] = 9;
                packet[0] = ch;
                packet[3] = 0;
                packet[2] = attribute;
                packet[4] = 1;
                packet[5] = 0;
                video_call(packet);
            }

            column = (int16_t)(uint16_t)((uint16_t)column + 1u);
            text.offset = (uint16_t)(text.offset + 1u);
        }
    }

    packet[1] = 2;
    packet[3] = 0;
    packet[7] = (uint8_t)row;
    packet[6] = (uint8_t)column;
    video_call(packet);
    if (gate_rtl_video_block_mode() != 0)
        gate_rtl_f8eb();
}
