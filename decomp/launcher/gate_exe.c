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

/* GATE.EXE file 0x0f61..0x1039. The original has a 0x38c-byte stack
 * frame: 0x8c bytes of picture work area, 0x300 bytes of temporary DAC
 * palette, and two 16-bit loop counters. This is a palette fade wrapped
 * around a set of still-unresolved image operations, not a timer table.
 *
 * CBW makes each original palette component a SIGNED byte, IMUL multiplies
 * by the factor, and the CWD/XOR/SUB/SAR sequence implements signed
 * division by 64 rounded toward zero. Given factors 0..60 and one-byte
 * components, the signed product stays within 16 bits.
 */
void gate_exe_0f61(uint16_t image)
{
    uint8_t work[0x8c];
    uint8_t faded[0x300];
    int16_t factor;
    int16_t i;

    gate_far_0f10_0d8e();
    gate_far_0f10_1680(gate_stack_pointer(work));
    gate_far_0f10_112e(gate_stack_pointer(work), image, 0);

    for (i = 0; i < 0x300; ++i)
        faded[i] = 0;
    gate_far_0cdc_04d2(gate_stack_pointer(faded));
    gate_far_0f10_13d2(gate_stack_pointer(work));
    gate_far_0f10_16c6(gate_stack_pointer(work));

    for (factor = 0; factor < 64; factor += 4) {
        for (i = 0; i < 0x300; ++i) {
            int16_t product = (int16_t)((int8_t)gate_far_byte(
                gate_ds_word(0x4e42), (uint16_t)(0x0fdc + i)) * factor);
            faded[i] = (uint8_t)(product / 64);
        }
        gate_far_0cdc_04d2(gate_stack_pointer(faded));
    }
    gate_far_0cdc_04d2(far_pointer(0x1a7a, 0x0fdc));
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

/* GATE.EXE file 0x1302..0x1a13. This is the WHOLE enclosing launcher
 * presentation function: 0x13fc is a branch target, not a function entry.
 *
 * Call argument ordering below follows the original 16-bit C stack, with
 * the leftmost argument closest to the return address. The five-argument
 * 0cdc:0aee service remains deliberately unnamed: its meaning is unknown.
 * No frame-buffer schedule is used to reconstruct this control flow.
 *
 * The `goto cleanup` statements reproduce jumps to 0x1a09. The short path
 * skips that cleanup and joins only at 0x1a0e. The signed comparison at
 * 0x1392 is likewise preserved (JGE, not unsigned JAE).
 */
void gate_exe_1302(void)
{
    uint16_t mode;

    gate_far_set_word(gate_ds_word(0x4e46), 0x0660, 0x0401);
    gate_far_set_word(gate_ds_word(0x4e48), 0x065c, 0x007f);
    gate_far_set_word(gate_ds_word(0x4e4a), 0x065e, 1);
    gate_far_set_word(gate_ds_word(0x4e4c), 0x064e, 0);
    gate_far_set_word(gate_ds_word(0x4e4e), 0x0658,
                      (uint16_t)(gate_far_0e2c_0094(0x1000) - 0x12b2));
    gate_far_0a46_01ec();

    mode = gate_far_word(gate_ds_word(0x4e2a), 0x0fce);
    if (mode == 0) {
        gate_far_0f10_1014(0x8101, 1);
        gate_far_0f10_1014(0x8100, 1);
        gate_exe_0f18(2000);
    } else {
        gate_exe_1a14();
    }

    mode = gate_far_word(gate_ds_word(0x4e2a), 0x0fce);
    if ((int16_t)mode < 2) {
        gate_far_0f10_0d1e();
        gate_far_0cdc_07d8();
        if (gate_far_word(gate_ds_word(0x4e2a), 0x0fce) == 1) {
            gate_far_0cdc_0aac(15);
            gate_far_0cdc_0aee(
                2, 0, 0,
                gate_far_word(gate_ds_word(0x4e30), 0x003e),
                gate_far_word(gate_ds_word(0x4e2e), 0x0042));
        }
        gate_far_0f10_1014(0x8102, 1);
        gate_exe_0f18(3000);
        goto finish;
    }

    if (gate_exe_0e37() != 0)
        goto cleanup;
    gate_far_0f10_0d8e();
    gate_far_0cdc_0004(3);
    gate_far_0e48_011b();
    gate_far_0cdc_07d8();
    gate_far_0f10_1014(0x3000, 1);
    if (gate_ds_word(0x35fc) == 0 && gate_exe_0e37() != 0)
        goto cleanup;
    gate_exe_0f18(1000);
    if (gate_ds_word(0x35fc) == 0 && gate_exe_0e37() != 0)
        goto cleanup;

    gate_exe_1c11(1);
    if (gate_ds_word(0x35fc) == 0 && gate_exe_0e37() != 0)
        goto cleanup;
    gate_exe_1c11(6);
    if (gate_ds_word(0x35fc) == 0 && gate_exe_0e37() != 0)
        goto cleanup;
    gate_exe_1c11(4);
    if (gate_ds_word(0x35fc) == 0 && gate_exe_0e37() != 0)
        goto cleanup;
    gate_exe_1c11(7);
    if (gate_ds_word(0x35fc) == 0 && gate_exe_0e37() != 0)
        goto cleanup;
    gate_exe_1c11(2);
    if (gate_ds_word(0x35fc) == 0 && gate_exe_0e37() != 0)
        goto cleanup;
    gate_exe_1c11(5);
    if (gate_ds_word(0x35fc) == 0 && gate_exe_0e37() != 0)
        goto cleanup;
    gate_exe_1c11(3);
    if (gate_ds_word(0x35fc) == 0 && gate_exe_0e37() != 0)
        goto cleanup;

    if (gate_far_0df2_000a() == 0)
        gate_far_0f10_15a6(0x3001, 26);
    if (gate_far_0df2_000a() == 0) {
        gate_far_0f10_1014(0x301b, 1);
        gate_exe_0f18(1000);
    }
    if (gate_far_0df2_000a() == 0) {
        gate_exe_1c6c();
        gate_exe_0f18(3000);
    }

    gate_ds_set_word(0x35fc, 1);
    if (gate_exe_0e37() != 0)
        goto cleanup;
    gate_exe_0f61(0x3105);
    gate_exe_0f18(1000);
    if (gate_exe_0e37() != 0)
        goto cleanup;

    gate_exe_117e(10, 120, 310, far_pointer(gate_ds_segment(), 0x363b));
    if (gate_exe_0e37() != 0)
        goto cleanup;
    gate_far_0f10_1014(0x3105, 0);
    gate_far_0f10_15a6(0x3106, 26);
    gate_exe_0f18(1000);
    if (gate_exe_0e37() != 0)
        goto cleanup;

    gate_exe_117e(10, 100, 180, far_pointer(gate_ds_segment(), 0x37bf));
    if (gate_exe_0e37() != 0)
        goto cleanup;
    gate_exe_0f61(0x3100);
    gate_exe_0f18(1000);
    gate_exe_117e(10, 10, 160, far_pointer(gate_ds_segment(), 0x3899));
    if (gate_exe_0e37() != 0)
        goto cleanup;
    gate_far_0f10_1014(0x3100, 0);
    gate_exe_117e(10, 10, 310, far_pointer(gate_ds_segment(), 0x395a));
    if (gate_exe_0e37() != 0)
        goto cleanup;

    gate_exe_0f61(0x3101);
    gate_exe_0f18(2000);
    gate_exe_117e(160, 10, 310, far_pointer(gate_ds_segment(), 0x3a65));
    if (gate_exe_0e37() != 0)
        goto cleanup;
    gate_exe_0f61(0x3102);
    gate_exe_0f18(2000);
    gate_exe_117e(180, 10, 310, far_pointer(gate_ds_segment(), 0x3b9b));
    if (gate_exe_0e37() != 0)
        goto cleanup;
    gate_far_0f10_1014(0x3102, 0);
    gate_exe_117e(180, 10, 310, far_pointer(gate_ds_segment(), 0x3c4f));
    if (gate_exe_0e37() != 0)
        goto cleanup;

    gate_far_0f10_1466(0x332a, 0x332b, 37);
    if (gate_exe_0e37() != 0)
        goto cleanup;
    gate_far_0f10_1466(0x3221, 0x323d, 42);
    gate_exe_0f18(1000);
    gate_exe_117e(130, 10, 310, far_pointer(gate_ds_segment(), 0x3d71));
    if (gate_exe_0e37() != 0)
        goto cleanup;
    gate_far_0f10_1014(0x3221, 0);
    gate_exe_117e(140, 10, 310, far_pointer(gate_ds_segment(), 0x3e79));
    if (gate_exe_0e37() != 0)
        goto cleanup;
    gate_far_0f10_1014(0x3221, 0);
    gate_exe_117e(140, 10, 310, far_pointer(gate_ds_segment(), 0x3f3b));
    if (gate_exe_0e37() != 0)
        goto cleanup;
    gate_far_0f10_1014(0x3221, 0);
    gate_exe_117e(140, 10, 310, far_pointer(gate_ds_segment(), 0x4086));
    if (gate_exe_0e37() != 0)
        goto cleanup;

    gate_exe_0f61(0x3103);
    gate_exe_0f18(1000);
    if (gate_exe_0e37() != 0)
        goto cleanup;
    gate_exe_117e(10, 10, 140, far_pointer(gate_ds_segment(), 0x4198));
    if (gate_exe_0e37() != 0)
        goto cleanup;
    gate_exe_0f61(0x3104);
    gate_exe_117e(60, 120, 310, far_pointer(gate_ds_segment(), 0x42ae));
    if (gate_exe_0e37() != 0)
        goto cleanup;
    gate_far_0f10_1014(0x3104, 0);
    gate_exe_117e(140, 120, 310, far_pointer(gate_ds_segment(), 0x436c));
    if (gate_exe_0e37() != 0)
        goto cleanup;

    gate_exe_0f61(0x3200);
    gate_exe_0f18(1000);
    if (gate_exe_0e37() != 0)
        goto cleanup;
    gate_exe_117e(120, 10, 310, far_pointer(gate_ds_segment(), 0x4436));
    if (gate_exe_0e37() != 0)
        goto cleanup;
    gate_far_0f10_1466(0x3200, 0x3201, 32);
    gate_far_0f10_1466(0x3221, 0x3222, 27);
    gate_far_0f10_1466(0x3300, 0x3301, 41);
    gate_exe_0f18(2000);
    if (gate_exe_0e37() != 0)
        goto cleanup;

    gate_exe_0f61(0x3a04);
    gate_exe_0f18(2000);
    if (gate_exe_0e37() != 0)
        goto cleanup;
    gate_exe_117e(10, 100, 160, far_pointer(gate_ds_segment(), 0x45c4));
    if (gate_exe_0e37() != 0)
        goto cleanup;
    gate_exe_0f61(0x332a);
    gate_exe_0f18(1000);
    if (gate_exe_0e37() != 0)
        goto cleanup;
    gate_exe_117e(10, 10, 310, far_pointer(gate_ds_segment(), 0x46c5));
    if (gate_exe_0e37() != 0)
        goto cleanup;
    gate_far_0f10_1014(0x332a, 0);
    gate_exe_117e(10, 10, 310, far_pointer(gate_ds_segment(), 0x4820));
    gate_exe_0e37();  /* 0x1a05: return value discarded */

cleanup:
    gate_far_0f10_0d8e();
finish:
    gate_far_0df2_0128();
}

/* GATE.EXE file 0x1a14..0x1c10. Intro title/palette interlude called from
 * 0x138a when the original display-mode word is nonzero. The four display
 * mode comparisons are separate loads, not a cached flag. Their JGE is
 * signed. No picture geometry or frame-capture timing was guessed here.
 *
 * In mode <2 only one palette byte changes per stage. In mode >=2 the
 * relevant triples fade 63..0, 0..63, 63..0, 63..0 in inclusive loops.
 * A pending key exits only after one of the three specified waits; unlike
 * 0x0e37 no key is consumed by this function.
 */
void gate_exe_1a14(void)
{
    int8_t i;

    for (i = 0; i < 45; ++i)
        gate_far_set_byte(gate_ds_word(0x4e42), (uint16_t)(0x0fdc + i), 63);
    gate_far_0f10_0c6e();
    gate_exe_0f18(250);
    gate_far_0f10_1014(0x8101, 1);

    if ((int16_t)gate_far_word(gate_ds_word(0x4e2a), 0x0fce) < 2) {
        gate_far_set_byte(gate_ds_word(0x4e42), 0x0fdd, 0);
        gate_far_0cdc_04d2(far_pointer(0x1a7a, 0x0fdc));
    } else {
        for (i = 63; i >= 0; --i) {
            gate_far_set_byte(gate_ds_word(0x4e42), 0x0fdf, (uint8_t)i);
            gate_far_set_byte(gate_ds_word(0x4e42), 0x0fe0, (uint8_t)i);
            gate_far_set_byte(gate_ds_word(0x4e42), 0x0fe1, (uint8_t)i);
            gate_far_0cdc_04d2(far_pointer(0x1a7a, 0x0fdc));
        }
    }
    gate_exe_0f18(2000);
    if (gate_far_0df2_000a() != 0)
        return;

    if ((int16_t)gate_far_word(gate_ds_word(0x4e2a), 0x0fce) < 2) {
        gate_far_set_byte(gate_ds_word(0x4e42), 0x0fdd, 63);
        gate_far_0cdc_04d2(far_pointer(0x1a7a, 0x0fdc));
    } else {
        for (i = 0; i <= 63; ++i) {
            gate_far_set_byte(gate_ds_word(0x4e42), 0x0fdf, (uint8_t)i);
            gate_far_set_byte(gate_ds_word(0x4e42), 0x0fe0, (uint8_t)i);
            gate_far_set_byte(gate_ds_word(0x4e42), 0x0fe1, (uint8_t)i);
            gate_far_0cdc_04d2(far_pointer(0x1a7a, 0x0fdc));
        }
    }
    gate_exe_0f18(500);
    if (gate_far_0df2_000a() != 0)
        return;

    gate_far_0f10_1014(0x8100, 1);
    if ((int16_t)gate_far_word(gate_ds_word(0x4e2a), 0x0fce) < 2) {
        gate_far_set_byte(gate_ds_word(0x4e42), 0x0fde, 0);
        gate_far_0cdc_04d2(far_pointer(0x1a7a, 0x0fdc));
    } else {
        for (i = 63; i >= 0; --i) {
            gate_far_set_byte(gate_ds_word(0x4e42), 0x0fe2, (uint8_t)i);
            gate_far_set_byte(gate_ds_word(0x4e42), 0x0fe3, (uint8_t)i);
            gate_far_set_byte(gate_ds_word(0x4e42), 0x0fe4, (uint8_t)i);
            gate_far_0cdc_04d2(far_pointer(0x1a7a, 0x0fdc));
        }
    }
    gate_exe_0f18(2000);
    if (gate_far_0df2_000a() != 0)
        return;

    if ((int16_t)gate_far_word(gate_ds_word(0x4e2a), 0x0fce) < 2) {
        gate_far_set_byte(gate_ds_word(0x4e42), 0x0fdf, 0);
        gate_far_0cdc_04d2(far_pointer(0x1a7a, 0x0fdc));
    } else {
        for (i = 63; i >= 0; --i) {
            gate_far_set_byte(gate_ds_word(0x4e42), 0x0fe5, (uint8_t)i);
            gate_far_set_byte(gate_ds_word(0x4e42), 0x0fe6, (uint8_t)i);
            gate_far_set_byte(gate_ds_word(0x4e42), 0x0fe7, (uint8_t)i);
            gate_far_0cdc_04d2(far_pointer(0x1a7a, 0x0fdc));
        }
    }
    gate_exe_0f18(2000);
}
