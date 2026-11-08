/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* DOS-time boundary for the linked EXE/RTL clock services.
 * Original RTL:8bb6..8cb7 samples INT 21h/AH=2Ah and AH=2Ch and writes
 * ten bytes (epoch seconds, milliseconds, timezone minutes, DST flag).
 * This adapter retains its 1980 epoch arithmetic and historical US DST
 * cutoff arithmetic. The separate animation wait below remains a bounded
 * DOS clock approximation, not the original animation event loop. */
#include "GATEMEM.H"
#include "RTLLAY.H"
#include <dos.h>

#define GATE_DOS_DAY_MILLISECONDS 86400000ul

static uint32_t gate_dos_clock_baseline;
static int gate_dos_clock_started;

/* GATE.RTL's relocatable month-start table is used with month-1. The
 * original test for leap years uses year & 3, not Gregorian centuries. */
static const uint16_t gate_days_before_month[13] = {
    0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365
};

void gate_linked_initialize_runtime_timezone(void);

/* GATE.RTL:8d9c..8e7d: the C 5.1 US daylight-saving rule, including
 * April's 1987 transition from last to first Sunday. The date-to-epoch
 * converter passes HOURS at tm+4, while the time-sample DST status
 * accidentally supplies SECONDS at that same offset. Both calls must
 * remain separate to preserve the two different original results. */
static uint16_t gate_dos_daylight_saving(uint16_t year, uint16_t month,
                                         uint16_t ordinal_day, uint16_t hour_field)
{
    int32_t years_since_1970;
    int32_t cutoff;
    int32_t weekday;
    int32_t boundary;
    if (month < 4u || month > 10u)
        return 0u;
    if (month > 4u && month < 10u)
        return 1u;

    if (month == 4u && year > 1986u)
        cutoff = (int32_t)gate_days_before_month[3] + 7l;
    else
        cutoff = (int32_t)gate_days_before_month[month];
    if ((year & 3u) == 0u)
        ++cutoff;
    years_since_1970 = (int32_t)year - 1970l;
    /* This is the actual RTL calculation: +4, signed divide by seven,
     * then subtract the remainder from the candidate ordinal day. */
    weekday = (years_since_1970 * 365l +
               (years_since_1970 + 1l) / 4l + cutoff + 4l) % 7l;
    boundary = cutoff - weekday;
    if (month == 4u)
        return (int32_t)ordinal_day > boundary ||
               ((int32_t)ordinal_day == boundary && hour_field >= 2u);
    return (int32_t)ordinal_day < boundary ||
           ((int32_t)ordinal_day == boundary && hour_field < 1u);
}

/* GATE.RTL:8e7e..8fa0: 1980-relative days converted to Unix seconds.
 * In the RTL the 32-bit unsigned results wrap naturally at DWORD width. */
static uint32_t gate_dos_calendar_seconds(uint16_t year, uint16_t month,
                                           uint16_t day, uint16_t hour,
                                           uint16_t minute, uint16_t second)
{
    uint32_t years = (uint32_t)(year - 1980u);
    uint32_t days = years * 365ul + (years + 3ul) / 4ul +
                    (uint32_t)gate_days_before_month[month - 1u] +
                    (uint32_t)(day - 1u);
    if ((year & 3u) == 0u && month > 2u)
        ++days;
    return days * 86400ul + (uint32_t)hour * 3600ul +
           (uint32_t)minute * 60ul + (uint32_t)second + 315532800ul;
}

/* Original linked 12a5:34d8: output is a FAR pointer to the ten-byte
 * RTL sample. This call supplies the real DOS calendar to the recovered
 * elapsed-time source, rather than inventing an elapsed-time constant. */
void gate_linked_read_runtime_time_sample(GateFarPtr destination)
{
    union REGS input, output;
    uint16_t year, month, day, hour, minute, second, hundredths;
    uint16_t ordinal_day, daylight = 0u, epoch_daylight = 0u;
    uint32_t epoch;
    int32_t timezone;

    gate_linked_initialize_runtime_timezone();
    timezone = (int32_t)((uint32_t)gate_ds_word(GATE_DS_TIMEZONE_OFFSET_LOW) |
                         ((uint32_t)gate_ds_word(GATE_DS_TIMEZONE_OFFSET_HIGH) << 16));

    input.h.ah = 0x2au;
    int86(0x21, &input, &output);
    year = output.x.cx;
    month = output.h.dh;
    day = output.h.dl;

    input.h.ah = 0x2cu;
    int86(0x21, &input, &output);
    hour = output.h.ch;
    minute = output.h.cl;
    second = output.h.dh;
    hundredths = output.h.dl;

    epoch = gate_dos_calendar_seconds(year, month, day, hour, minute, second);
    ordinal_day = (uint16_t)(gate_days_before_month[month - 1u] + day);
    if ((year & 3u) == 0u && month > 2u)
        ++ordinal_day;
    if (gate_ds_word(GATE_DS_TIMEZONE_HAS_DAYLIGHT_NAME) != 0u) {
        epoch_daylight = gate_dos_daylight_saving(year, month, ordinal_day, hour);
        daylight = gate_dos_daylight_saving(year, month, ordinal_day, second);
    }
    epoch += (uint32_t)timezone;
    if (epoch_daylight != 0u)
        epoch -= 3600ul;

    gate_far_set_word(destination.segment, destination.offset, (uint16_t)epoch);
    gate_far_set_word(destination.segment, (uint16_t)(destination.offset + 2u),
                      (uint16_t)(epoch >> 16));
    gate_far_set_word(destination.segment, (uint16_t)(destination.offset + 4u),
                      (uint16_t)(hundredths * 10u));
    gate_far_set_word(destination.segment, (uint16_t)(destination.offset + 6u),
                      (uint16_t)(timezone / 60l));
    gate_far_set_word(destination.segment, (uint16_t)(destination.offset + 8u), daylight);
}

/* DOS AH=2Ch: CH hours, CL minutes, DH seconds, DL hundredths. */
static uint32_t dos_time_of_day_ms(void)
{
    union REGS input, output;
    uint32_t seconds;
    input.h.ah = 0x2cu;
    int86(0x21, &input, &output);
    seconds = ((uint32_t)output.h.ch * 60ul + (uint32_t)output.h.cl) * 60ul +
              (uint32_t)output.h.dh;
    return seconds * 1000ul + (uint32_t)output.h.dl * 10ul;
}

static uint32_t gate_dos_elapsed_milliseconds(void)
{
    uint32_t now = dos_time_of_day_ms();
    if (!gate_dos_clock_started) {
        gate_dos_clock_baseline = now;
        gate_dos_clock_started = 1;
    }
    if (now >= gate_dos_clock_baseline)
        return now - gate_dos_clock_baseline;
    return GATE_DOS_DAY_MILLISECONDS - gate_dos_clock_baseline + now;
}

uint32_t gate_linked_exe_elapsed_milliseconds(void)
{
    return gate_dos_elapsed_milliseconds();
}

uint32_t gate_linked_animation_elapsed_milliseconds(void)
{
    return gate_dos_elapsed_milliseconds();
}

/* No simulated clock advance: wait until the real DOS clock progresses. */
void gate_linked_wait_animation_delay(uint32_t delay)
{
    uint32_t start = gate_dos_elapsed_milliseconds();
    while (gate_dos_elapsed_milliseconds() - start < delay) {
        /* Original animation wait may also service input/frame events. */
    }
}

void gate_rtl_graphics_delay(uint32_t delay)
{
    gate_linked_wait_animation_delay(delay);
}
