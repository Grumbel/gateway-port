/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_rtl.h"
#include "gate_c_runtime_constants.h"

#include <stdlib.h>

/* Linked 12a5:34d8 (RTL:8bb6) fills a time sample through an SS:offset
 * pointer. Its calendar conversion is not reconstructed here. */
void gate_linked_read_runtime_time_sample(GateFarPtr destination);

/* Microsoft-style FAR C runtime imports at linked 12a5:2cd0, :2bd2 and
 * :2c38, identified by their original 8086 bodies: getenv, strncpy, atol.
 * This translation keeps the original FAR layout instead of consulting the
 * host's timezone environment or copying native pointers into guest RAM. */
GateFarPtr gate_linked_get_runtime_environment(GateFarPtr name);
GateFarPtr gate_linked_copy_runtime_string(GateFarPtr destination, GateFarPtr source,
                               uint16_t length);
uint32_t gate_linked_parse_runtime_integer(GateFarPtr number);

/* GATE.RTL:8cc8..8d9a, linked 12a5:35ea. Read the DOS process's TZ
 * environment setting, copy its three-byte standard zone, and convert the
 * following numeric offset into signed seconds. The suffix search is NOT a
 * conventional timezone parser: it advances at most three bytes that have
 * the C runtime's _DIGIT bit (4) or are '-'. '+' is not skipped, and a long
 * number can consequently be mistaken for the daylight zone suffix.
 * Those are original behaviors, retained deliberately. */
void gate_rtl_parse_timezone_environment(void)
{
    GateFarPtr value = gate_linked_get_runtime_environment((GateFarPtr){gate_ds_segment(), GATE_DS_TIMEZONE_ENV_NAME});
    uint16_t skip = 0;

    if ((value.offset | value.segment) == 0 ||
        gate_far_byte(value.segment, value.offset) == 0)
        return;

    gate_linked_copy_runtime_string((GateFarPtr){gate_ds_segment(), GATE_DS_TIMEZONE_STANDARD_NAME},
                                    value, GATE_CRT_TIMEZONE_ABBREVIATION_BYTES);
    value.offset = (uint16_t)(value.offset + GATE_CRT_TIMEZONE_ABBREVIATION_BYTES);
    /* The MUL return is DX:AX, both words must be retained. */
    {
        uint32_t seconds = gate_rtl_multiply_32(gate_linked_parse_runtime_integer(value), GATE_CRT_SECONDS_PER_HOUR);
        gate_ds_set_word(GATE_DS_TIMEZONE_OFFSET_LOW, (uint16_t)seconds);
        gate_ds_set_word(GATE_DS_TIMEZONE_OFFSET_HIGH, (uint16_t)(seconds >> GATE_CRT_WORD_BITS));
    }

    while (skip < GATE_CRT_TIMEZONE_ABBREVIATION_BYTES) {
        uint8_t ch = gate_far_byte(value.segment,
                                   (uint16_t)(value.offset + skip));
        int16_t signed_ch = (int8_t)ch;
        if (ch == 0 || ((gate_far_byte(gate_ds_segment(),
            (uint16_t)(GATE_DS_CRT_CHAR_CLASS_TABLE + signed_ch)) & GATE_CRT_TIMEZONE_DIGIT_CLASS_BIT) == 0 && ch != '-'))
            break;
        ++skip;
    }

    if (gate_far_byte(value.segment, (uint16_t)(value.offset + skip)) != 0) {
        gate_linked_copy_runtime_string((GateFarPtr){gate_ds_segment(), GATE_DS_TIMEZONE_DAYLIGHT_NAME},
                            (GateFarPtr){value.segment,
                                         (uint16_t)(value.offset + skip)}, GATE_CRT_TIMEZONE_ABBREVIATION_BYTES);
    } else {
        gate_far_set_byte(gate_ds_segment(), GATE_DS_TIMEZONE_DAYLIGHT_NAME, 0);
    }
    gate_ds_set_word(GATE_DS_TIMEZONE_HAS_DAYLIGHT_NAME,
                     gate_far_byte(gate_ds_segment(), GATE_DS_TIMEZONE_DAYLIGHT_NAME) >= 1u);
}

/* GATE.RTL:8cb8..8cc7, linked 12a5:35da. The original startup flag is
 * relocated to DS:6470. Repeated calls skip initialization; the one-time
 * initializer may update the same flag, so the caller increments its
 * actual post-call value rather than blindly setting it to one. */
void gate_rtl_initialize_timezone_once(void)
{
    if (gate_ds_word(GATE_DS_TIMEZONE_INITIALIZED) == 0) {
        gate_rtl_parse_timezone_environment();
        gate_ds_set_word(GATE_DS_TIMEZONE_INITIALIZED, (uint16_t)(gate_ds_word(GATE_DS_TIMEZONE_INITIALIZED) + 1u));
    }
}

void gate_linked_initialize_runtime_timezone(void)
{
    gate_rtl_initialize_timezone_once();
}

/* GATE.RTL:929e..9339, linked 12a5:3bc0: signed 32-bit quotient.
 * The 8086 routine tests both sign words, divides magnitudes and restores
 * the sign; the returned DX:AX wraps to 32 bits. Zero divisor faults in
 * the original DIV and is never silently replaced by a result. */
uint32_t gate_rtl_signed_divide_32(int32_t dividend, int32_t divisor)
{
    if (divisor == 0)
        abort(); /* original divide trap; no recovery value */
    return (uint32_t)((int64_t)dividend / (int64_t)divisor);
}

/* GATE.RTL:933a..936b, linked 12a5:3c5c. The 8086 MUL sequence returns
 * only DX:AX; carries above 32 bits are discarded. */
uint32_t gate_rtl_multiply_32(uint32_t lhs, uint32_t rhs)
{
    return lhs * rhs;
}

/* GATE.RTL:8fa2..8ff8, linked 12a5:38c4.
 * The original resets the word at the FAR segment in DS:4e24, then reads
 * a time sample into ten stack bytes. Words 0/2 contain a 32-bit whole
 * time count and word 4 its fractional component. The global baseline is
 * DS:3584/3586, with fractional baseline DS:3588.
 *
 * Preserve the original signed 16-bit fractional subtraction (CBW/CWD)
 * and the 32-bit modular multiplication. This is source recovery only:
 * the host still supplies the elapsed clock directly for playback. */
uint32_t gate_rtl_elapsed_milliseconds(void)
{
    uint8_t sample[GATE_CRT_TIME_SAMPLE_BYTES] = {0};
    GateFarPtr destination = gate_stack_pointer(sample);
    uint32_t current;
    uint32_t baseline;
    int16_t fraction;

    gate_far_set_word(gate_ds_word(GATE_DS_CLOCK_SAMPLE_STATUS_SEGMENT),
                      GATE_DS_CLOCK_SAMPLE_STATUS, 0);
    gate_linked_read_runtime_time_sample(destination);
    current = (uint32_t)gate_far_word(destination.segment, destination.offset) |
              ((uint32_t)gate_far_word(destination.segment,
                                      (uint16_t)(destination.offset + GATE_CRT_TIME_SAMPLE_HIGH_WORD_OFFSET)) << GATE_CRT_WORD_BITS);
    baseline = (uint32_t)gate_ds_word(GATE_DS_CLOCK_BASELINE_LOW) |
               ((uint32_t)gate_ds_word(GATE_DS_CLOCK_BASELINE_HIGH) << GATE_CRT_WORD_BITS);
    fraction = (int16_t)(gate_far_word(destination.segment,
                                     (uint16_t)(destination.offset + GATE_CRT_TIME_SAMPLE_FRACTION_WORD_OFFSET)) -
                         gate_ds_word(GATE_DS_CLOCK_BASELINE_FRACTION));
    return gate_rtl_multiply_32(current - baseline, GATE_CRT_MILLISECONDS_PER_SECOND) +
           (uint32_t)(int32_t)fraction;
}
