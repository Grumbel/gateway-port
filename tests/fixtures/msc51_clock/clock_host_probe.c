/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "GATEMEM.H"
#include "RTLLAY.H"
#include <dos.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

void gate_linked_read_runtime_time_sample(GateFarPtr destination);

static uint8_t guest_ds[65536];
static uint8_t output_buffer[65536];
static uint16_t mock_year, mock_month, mock_day;
static uint8_t mock_hour, mock_minute, mock_second, mock_hundredths;
static unsigned date_calls, time_calls, timezone_calls;

void gate_linked_initialize_runtime_timezone(void) { ++timezone_calls; }
uint16_t gate_ds_segment(void) { return 0x1000u; }
uint8_t gate_far_byte(uint16_t segment, uint16_t offset)
{
    assert(segment == 0x1000u || segment == 0x2000u);
    return segment == 0x1000u ? guest_ds[offset] : output_buffer[offset];
}
void gate_far_set_byte(uint16_t segment, uint16_t offset, uint8_t value)
{
    assert(segment == 0x1000u || segment == 0x2000u);
    if (segment == 0x1000u) guest_ds[offset] = value;
    else output_buffer[offset] = value;
}
uint16_t gate_far_word(uint16_t segment, uint16_t offset)
{
    return (uint16_t)(gate_far_byte(segment, offset) |
                      ((uint16_t)gate_far_byte(segment, (uint16_t)(offset + 1u)) << 8));
}
void gate_far_set_word(uint16_t segment, uint16_t offset, uint16_t value)
{
    gate_far_set_byte(segment, offset, (uint8_t)value);
    gate_far_set_byte(segment, (uint16_t)(offset + 1u), (uint8_t)(value >> 8));
}
uint16_t gate_ds_word(uint16_t offset)
{
    return gate_far_word(0x1000u, offset);
}
void gate_ds_set_word(uint16_t offset, uint16_t value)
{
    gate_far_set_word(0x1000u, offset, value);
}

int int86(int interrupt, union REGS *input, union REGS *output)
{
    assert(interrupt == 0x21);
    memset(output, 0, sizeof *output);
    if (input->h.ah == 0x2au) {
        ++date_calls;
        output->x.cx = mock_year;
        output->h.dh = (uint8_t)mock_month;
        output->h.dl = (uint8_t)mock_day;
    } else {
        assert(input->h.ah == 0x2cu);
        ++time_calls;
        output->h.ch = mock_hour;
        output->h.cl = mock_minute;
        output->h.dh = mock_second;
        output->h.dl = mock_hundredths;
    }
    return 0;
}

static uint32_t read_sample(uint16_t year, uint16_t month, uint16_t day,
                            uint8_t hour, uint8_t minute, uint8_t second,
                            uint8_t hundredths, int32_t timezone, uint16_t dst,
                            uint16_t *fraction, int16_t *minutes, uint16_t *daylight)
{
    GateFarPtr pointer;
    uint32_t seconds;
    mock_year = year;
    mock_month = month;
    mock_day = day;
    mock_hour = hour;
    mock_minute = minute;
    mock_second = second;
    mock_hundredths = hundredths;
    gate_ds_set_word(GATE_DS_TIMEZONE_OFFSET_LOW, (uint16_t)timezone);
    gate_ds_set_word(GATE_DS_TIMEZONE_OFFSET_HIGH, (uint16_t)((uint32_t)timezone >> 16));
    gate_ds_set_word(GATE_DS_TIMEZONE_HAS_DAYLIGHT_NAME, dst);
    /* Verify the original 8086 offset arithmetic across 0xffff. */
    pointer.segment = 0x2000u;
    pointer.offset = 0xfffcu;
    gate_linked_read_runtime_time_sample(pointer);
    seconds = (uint32_t)gate_far_word(pointer.segment, pointer.offset) |
              ((uint32_t)gate_far_word(pointer.segment,
                                      (uint16_t)(pointer.offset + 2u)) << 16);
    *fraction = gate_far_word(pointer.segment, (uint16_t)(pointer.offset + 4u));
    *minutes = (int16_t)gate_far_word(pointer.segment, (uint16_t)(pointer.offset + 6u));
    *daylight = gate_far_word(pointer.segment, (uint16_t)(pointer.offset + 8u));
    return seconds;
}

int main(void)
{
    uint16_t fraction, daylight;
    int16_t minutes;
    uint32_t epoch;
    epoch = read_sample(1980, 1, 1, 0, 0, 0, 0, 0, 0, &fraction, &minutes, &daylight);
    assert(epoch == 315532800ul && fraction == 0u && minutes == 0 && daylight == 0u);
    epoch = read_sample(2000, 2, 29, 23, 59, 59, 99, 0, 0, &fraction, &minutes, &daylight);
    assert(epoch == 951868799ul && fraction == 990u);
    epoch = read_sample(2026, 1, 15, 12, 0, 0, 3, -18000l, 1u,
                        &fraction, &minutes, &daylight);
    assert(epoch == 1768460400ul && fraction == 30u && minutes == -300 && daylight == 0u);
    epoch = read_sample(2026, 7, 15, 12, 0, 0, 4, 3600l, 1u,
                        &fraction, &minutes, &daylight);
    assert(epoch == 1784116800ul && fraction == 40u && minutes == 60 && daylight == 1u);
    epoch = read_sample(1987, 4, 4, 0, 0, 0, 0, 0, 1u,
                        &fraction, &minutes, &daylight);
    assert(daylight == 0u);
    epoch = read_sample(1987, 4, 4, 0, 0, 2, 0, 0, 1u,
                        &fraction, &minutes, &daylight);
    /* Sampler uses seconds for DST status, epoch converter uses hours. */
    assert(daylight == 1u && epoch == 544492802ul);
    epoch = read_sample(1987, 4, 4, 2, 0, 0, 0, 0, 1u,
                        &fraction, &minutes, &daylight);
    assert(daylight == 0u && epoch == 544496400ul);
    epoch = read_sample(1987, 4, 5, 0, 0, 0, 0, 0, 1u,
                        &fraction, &minutes, &daylight);
    assert(daylight == 1u);
    assert(date_calls == 8u && time_calls == 8u && timezone_calls == 8u);
    puts("Clock host probe: calendar/DST/timezone: PASS");
    return 0;
}
