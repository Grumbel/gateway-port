/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_exe.h"

static GateFarPtr far_pointer(uint16_t segment, uint16_t offset)
{
    GateFarPtr result = {segment, offset};
    return result;
}

/* GATE.EXE file 0x0e37..0x0f17. Key handling and intro interruption.
 *
 * The RTL calls are still named by their original linked segment:offset.
 * Stack-check far call 02a5:0c08 is compiler machinery, not source logic.
 * The `key == 3` branch is intentionally not treated as ordinary Escape.
 */
uint16_t gate_exe_0e37(void)
{
    uint16_t key;
    uint16_t width;
    uint16_t height;

    if (gate_far_0df2_000a() == 0) {
        if ((gate_far_0e48_07fe() & 2) != 0)
            return 1;
        return 0;
    }

    key = gate_far_0df2_0030();
    if (key == 3) {
        gate_far_123f_0004();
        return 0;
    }
    if (key == 27)
        return 1;

    if (gate_ds_word(0x35fc) != 0)
        return 0;
    gate_ds_set_word(0x35fc, 1);
    gate_far_0c4d_018d(1);
    gate_far_0c4d_061b(gate_far_word(gate_ds_word(0x4e40), 0x54), 0xffff);
    width = gate_far_0c4d_05be(far_pointer(gate_ds_segment(), 0x35fe));
    height = gate_far_0c4d_0511();
    gate_far_0c4d_0653(
        (int16_t)((int16_t)(gate_far_word(gate_ds_word(0x4e30), 0x3e) - width) / 2),
        (int16_t)(gate_far_word(gate_ds_word(0x4e2e), 0x42) - height - 1));
    gate_far_0c4d_0698(far_pointer(gate_ds_segment(), 0x35fe));
    if (gate_far_word(gate_ds_word(0x4e2a), 0x0fce) != 3) {
        gate_exe_0f18(3500);
        gate_far_0df2_00f8();
    }
    return 0;
}

/* GATE.EXE file 0x0f18..0x0f60. Interruptible clock delay.
 *
 * Original assembly compares HIGH words as signed 16-bit numbers and LOW
 * words as unsigned, not as a plain unsigned 32-bit timestamp comparison.
 * The delay is sign-extended by CWD and then added with ADC carry.
 */
void gate_exe_0f18(int16_t delay)
{
    uint32_t deadline;
    uint32_t now;

    deadline = gate_far_02a5_38c4() + (uint32_t)(int32_t)delay;
    for (;;) {
        now = gate_far_02a5_38c4();
        if ((int16_t)(now >> 16) > (int16_t)(deadline >> 16) ||
            ((int16_t)(now >> 16) == (int16_t)(deadline >> 16) &&
             (uint16_t)now >= (uint16_t)deadline))
            break;
        if (gate_far_0df2_000a() != 0)
            break;
        if (gate_far_0e48_07fe() != 0)
            break;
    }
}

/* GATE.EXE file 0x1c11..0x1c6b. One of the seven red DAC bars.
 *
 * `bar * 3 + 45` is computed by signed IMUL, low word only; the loop byte
 * starts at zero and runs through 63 inclusive. If a key is pending, the
 * ENTIRE fade and its final 500ms wait are skipped.
 */
void gate_exe_1c11(int16_t bar)
{
    uint16_t index;
    uint8_t value;

    if (gate_far_0df2_000a() != 0)
        return;

    index = (uint16_t)(45 + 3 * (int32_t)bar);
    for (value = 0; value <= 63; ++value) {
        gate_far_set_byte(gate_ds_word(0x4e42), (uint16_t)(0x0fdc + index), value);
        gate_far_0cdc_04d2(far_pointer(0x1a7a, 0x0fdc));
    }
    gate_exe_0f18(500);
}

/* GATE.EXE file 0x1c6c..0x1caa. Grayscale title credit reveal.
 * Unlike 0x1c11, there is no keyboard test and no final delay.
 */
void gate_exe_1c6c(void)
{
    uint8_t value;
    uint16_t segment;

    for (value = 0; value < 64; ++value) {
        segment = gate_ds_word(0x4e42);
        gate_far_set_byte(segment, 0x1021, value);
        gate_far_set_byte(segment, 0x1022, value);
        gate_far_set_byte(segment, 0x1023, value);
        gate_far_0cdc_04d2(far_pointer(0x1a7a, 0x0fdc));
    }
}
