/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "GATEMEM.H"
#include "RTLLAY.H"
#include <dos.h>
#include <stdio.h>

uint32_t gate_linked_exe_elapsed_milliseconds(void);
uint32_t gate_linked_animation_elapsed_milliseconds(void);
void gate_linked_wait_animation_delay(uint32_t delay);
void gate_rtl_graphics_delay(uint32_t delay);
void gate_linked_read_runtime_time_sample(GateFarPtr destination);

/* The production timezone initializer lives in recovered M00.C; its
 * sample-facing ABI is checked with a narrow stand-in in this DOS probe. */
static uint16_t timezone_init_calls;
void gate_linked_initialize_runtime_timezone(void)
{
    ++timezone_init_calls;
}

static uint32_t sample_seconds(GateFarPtr pointer)
{
    return (uint32_t)gate_far_word(pointer.segment, pointer.offset) |
           ((uint32_t)gate_far_word(pointer.segment,
                                   (uint16_t)(pointer.offset + 2u)) << 16);
}

int main(void)
{
    uint32_t first = gate_linked_exe_elapsed_milliseconds();
    uint32_t second = gate_linked_animation_elapsed_milliseconds();
    uint32_t end;
    uint32_t zero_zone, one_hour_zone;
    uint8_t sample[10];
    GateFarPtr destination;
    unsigned int segment = 0;
    /* Microsoft C 5.1 rejects initializing a struct local from a return
     * value (C2074), but supports ordinary struct assignment. */
    destination = gate_stack_pointer(sample);
    if (_dos_allocmem(0x0400u, &segment) != 0) {
        puts("Clock probe: guest DS allocation failed");
        return 1;
    }
    gate_dos_bind_ds((uint16_t)segment);
    gate_ds_set_word(GATE_DS_TIMEZONE_OFFSET_LOW, 0u);
    gate_ds_set_word(GATE_DS_TIMEZONE_OFFSET_HIGH, 0u);
    gate_ds_set_word(GATE_DS_TIMEZONE_HAS_DAYLIGHT_NAME, 0u);
    gate_linked_read_runtime_time_sample(destination);
    zero_zone = sample_seconds(destination);
    if (zero_zone < 315532800ul ||
        gate_far_word(destination.segment, (uint16_t)(destination.offset + 4u)) >= 1000u ||
        gate_far_word(destination.segment, (uint16_t)(destination.offset + 6u)) != 0u ||
        gate_far_word(destination.segment, (uint16_t)(destination.offset + 8u)) != 0u) {
        puts("Clock probe: DOS calendar sample layout failed");
        (void)_dos_freemem(segment);
        return 1;
    }
    gate_ds_set_word(GATE_DS_TIMEZONE_OFFSET_LOW, 3600u);
    gate_linked_read_runtime_time_sample(destination);
    one_hour_zone = sample_seconds(destination);
    if (one_hour_zone < zero_zone + 3598ul ||
        one_hour_zone > zero_zone + 3602ul ||
        gate_far_word(destination.segment, (uint16_t)(destination.offset + 6u)) != 60u ||
        timezone_init_calls != 2u) {
        puts("Clock probe: timezone minute conversion failed");
        (void)_dos_freemem(segment);
        return 1;
    }
    (void)_dos_freemem(segment);
    if (second < first || second - first > 1000ul) {
        puts("Clock probe: EXE and RTL clock disagree");
        return 1;
    }
    gate_rtl_graphics_delay(20ul);
    gate_linked_wait_animation_delay(0ul);
    end = gate_linked_exe_elapsed_milliseconds();
    if (end < first + 20ul || end - first > 2000ul) {
        puts("Clock probe: DOS elapsed time did not advance by 20 ms");
        return 1;
    }
    printf("Clock probe: PASS, %lu ms\n", (unsigned long)(end - first));
    return 0;
}
