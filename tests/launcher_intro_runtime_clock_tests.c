/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gate_exe.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t gate_rtl_elapsed_milliseconds(void);
uint32_t gate_rtl_multiply_32(uint32_t lhs, uint32_t rhs);
uint32_t gate_rtl_signed_divide_32(int32_t dividend, int32_t divisor);
void gate_rtl_initialize_timezone_once(void);
void gate_rtl_parse_timezone_environment(void);
void gate_linked_initialize_runtime_timezone(void);

static uint8_t stack_bytes[65536];
static uint8_t environment_bytes[65536];
static uint16_t ds[32768];
static uint16_t cursor_reset;
static uint32_t seconds;
static uint16_t fraction;
static unsigned calls;
static unsigned timezone_lookup_calls;
static unsigned timezone_copy_calls;
static unsigned timezone_number_calls;
static int timezone_present;
static uint16_t timezone_lookup_side_effect_flag;

uint16_t gate_ds_segment(void) { return 0x5555; }

uint16_t gate_ds_word(uint16_t offset) { return ds[offset / 2]; }
void gate_ds_set_word(uint16_t offset, uint16_t value) { ds[offset / 2] = value; }

uint8_t gate_far_byte(uint16_t segment, uint16_t offset)
{
    if (segment == 0x8800) return environment_bytes[offset];
    assert(segment == 0x5555);
    return (uint8_t)(ds[offset / 2] >> ((offset & 1u) * 8u));
}

void gate_far_set_byte(uint16_t segment, uint16_t offset, uint8_t value)
{
    assert(segment == 0x5555);
    uint16_t *word = &ds[offset / 2];
    unsigned shift = (offset & 1u) * 8u;
    *word = (uint16_t)((*word & ~(0xffu << shift)) | ((uint16_t)value << shift));
}

GateFarPtr gate_linked_get_runtime_environment(GateFarPtr name)
{
    assert(name.segment == 0x5555 && name.offset == 0x352c);
    assert(gate_far_byte(name.segment, name.offset) == 'T');
    assert(gate_far_byte(name.segment, (uint16_t)(name.offset + 1)) == 'Z');
    ++timezone_lookup_calls;
    if (timezone_lookup_side_effect_flag)
        gate_ds_set_word(0x6470, timezone_lookup_side_effect_flag);
    return timezone_present ? (GateFarPtr){0x8800, 0x0100} : (GateFarPtr){0, 0};
}

GateFarPtr gate_linked_copy_runtime_string(GateFarPtr dst, GateFarPtr src, uint16_t n)
{
    assert(dst.segment == 0x5555 && src.segment == 0x8800 && n == 3);
    assert(dst.offset == 0x353e || dst.offset == 0x3542);
    ++timezone_copy_calls;
    uint16_t i = 0;
    for (; i < n; ++i) {
        uint8_t ch = gate_far_byte(src.segment, (uint16_t)(src.offset + i));
        if (!ch) break;
        gate_far_set_byte(dst.segment, (uint16_t)(dst.offset + i), ch);
    }
    for (; i < n; ++i)
        gate_far_set_byte(dst.segment, (uint16_t)(dst.offset + i), 0);
    return dst;
}

uint32_t gate_linked_parse_runtime_integer(GateFarPtr text)
{
    assert(text.segment == 0x8800);
    ++timezone_number_calls;
    return (uint32_t)strtol((const char *)environment_bytes + text.offset, NULL, 10);
}

static void set_timezone(const char *text)
{
    memset(environment_bytes, 0, sizeof environment_bytes);
    assert(strlen(text) < sizeof environment_bytes - 0x0100);
    strcpy((char *)environment_bytes + 0x0100, text);
    timezone_present = 1;
}

static void check_zone(uint16_t at, const char *expected)
{
    for (unsigned i = 0; i < 3; ++i)
        assert(gate_far_byte(0x5555, (uint16_t)(at + i)) == (uint8_t)expected[i]);
}

GateFarPtr gate_stack_pointer(void *unused)
{
    (void)unused;
    return (GateFarPtr){0x9000, 0xfffe};
}

uint16_t gate_far_word(uint16_t segment, uint16_t offset)
{
    if (segment == 0x5555 && offset == 0x3582)
        return cursor_reset;
    assert(segment == 0x9000);
    return (uint16_t)(stack_bytes[offset] |
                      ((uint16_t)stack_bytes[(uint16_t)(offset + 1)] << 8));
}

void gate_far_set_word(uint16_t segment, uint16_t offset, uint16_t value)
{
    if (segment == 0x5555 && offset == 0x3582) {
        cursor_reset = value;
        return;
    }
    assert(segment == 0x9000);
    stack_bytes[offset] = (uint8_t)value;
    stack_bytes[(uint16_t)(offset + 1)] = (uint8_t)(value >> 8);
}

void gate_linked_read_runtime_time_sample(GateFarPtr destination)
{
    assert(cursor_reset == 0); /* reset precedes original sampler call */
    ++calls;
    gate_far_set_word(destination.segment, destination.offset, (uint16_t)seconds);
    gate_far_set_word(destination.segment, (uint16_t)(destination.offset + 2),
                      (uint16_t)(seconds >> 16));
    gate_far_set_word(destination.segment, (uint16_t)(destination.offset + 4), fraction);
}

int main(void)
{
    gate_far_set_byte(0x5555, 0x352c, 'T');
    gate_far_set_byte(0x5555, 0x352d, 'Z');
    gate_far_set_byte(0x5555, 0x352e, 0);
    /* The original runtime image has these two three-letter defaults. */
    for (unsigned i = 0; i < 3; ++i) {
        gate_far_set_byte(0x5555, (uint16_t)(0x353e + i), "PST"[i]);
        gate_far_set_byte(0x5555, (uint16_t)(0x3542 + i), "PDT"[i]);
        gate_far_set_byte(0x5555, (uint16_t)(0x33f7 + '0' + i), 4);
    }
    for (unsigned ch = '3'; ch <= '9'; ++ch)
        gate_far_set_byte(0x5555, (uint16_t)(0x33f7 + ch), 4);
    gate_ds_set_word(0x3538, 0xabcd);
    gate_linked_initialize_runtime_timezone();
    assert(timezone_lookup_calls == 1);
    assert(gate_ds_word(0x6470) == 1);
    assert(gate_ds_word(0x3538) == 0xabcd);
    gate_rtl_initialize_timezone_once();
    assert(timezone_lookup_calls == 1);
    set_timezone("PST8PDT");
    gate_ds_set_word(0x6470, 0);
    gate_rtl_initialize_timezone_once();
    assert(timezone_lookup_calls == 2);
    assert(gate_ds_word(0x6470) == 1);
    assert(timezone_copy_calls == 2 && timezone_number_calls == 1);
    check_zone(0x353e, "PST");
    check_zone(0x3542, "PDT");
    assert(((uint32_t)gate_ds_word(0x3538) |
            ((uint32_t)gate_ds_word(0x353a) << 16)) == 28800u);
    assert(gate_ds_word(0x353c) == 1);
    timezone_lookup_side_effect_flag = 7;
    gate_ds_set_word(0x6470, 0);
    gate_rtl_initialize_timezone_once();
    assert(gate_ds_word(0x6470) == 8); /* increment actual post-call value */
    timezone_lookup_side_effect_flag = 0;

    set_timezone("PST-8PDT");
    gate_rtl_parse_timezone_environment();
    assert(((uint32_t)gate_ds_word(0x3538) |
            ((uint32_t)gate_ds_word(0x353a) << 16)) == (uint32_t)-28800);
    check_zone(0x3542, "PDT");
    set_timezone("PST8");
    gate_rtl_parse_timezone_environment();
    assert(gate_far_byte(0x5555, 0x3542) == 0);
    assert(gate_ds_word(0x353c) == 0);

    /* Original parser quirk: '+' is not scanned as part of the number,
     * and only three characters of a long number are skipped. */
    set_timezone("PST+8PDT");
    gate_rtl_parse_timezone_environment();
    check_zone(0x3542, "+8P");
    set_timezone("PST1234ABC");
    gate_rtl_parse_timezone_environment();
    check_zone(0x3542, "4AB");

    set_timezone("");
    unsigned previous_copies = timezone_copy_calls;
    gate_rtl_parse_timezone_environment();
    assert(timezone_copy_calls == previous_copies);

    memset(stack_bytes, 0xcc, sizeof stack_bytes);
    ds[0x4e24 / 2] = 0x5555;
    ds[0x3584 / 2] = 0x2340;
    ds[0x3586 / 2] = 0x0001;
    ds[0x3588 / 2] = 1200;
    seconds = 0x00012345;
    fraction = 900;
    cursor_reset = 0xbeef;
    assert(gate_rtl_elapsed_milliseconds() == 4700);
    assert(calls == 1);

    ds[0x3584 / 2] = 0xfffe;
    ds[0x3586 / 2] = 0xffff;
    ds[0x3588 / 2] = 99;
    seconds = 2;
    fraction = 0xffff;
    cursor_reset = 0xbeef;
    assert(gate_rtl_elapsed_milliseconds() == 3900);
    assert(calls == 2);

    assert(gate_rtl_multiply_32(0xffffffffu, 1000u) == 0xfffffc18u);
    assert(gate_rtl_multiply_32(0x12345678u, 0x87654321u) ==
           (uint32_t)(0x12345678u * 0x87654321u));
    assert((int32_t)gate_rtl_signed_divide_32(3600, 60) == 60);
    assert((int32_t)gate_rtl_signed_divide_32(-3600, 60) == -60);
    assert((int32_t)gate_rtl_signed_divide_32(3600, -60) == -60);
    assert((int32_t)gate_rtl_signed_divide_32(-3600, -60) == 60);
    assert((int32_t)gate_rtl_signed_divide_32(-100, 60) == -1);
    assert(gate_rtl_signed_divide_32((int32_t)0x80000000u, -1) == 0x80000000u);
    puts("RTL:8fa2 clock and RTL:933a multiply: OK");
    return 0;
}
