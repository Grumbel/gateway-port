/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Independent ordering/argument oracle for RTL:157b8..157e4. */
#include <assert.h>
#include <stdint.h>

void gate_linked_handle_control_key(void);

static unsigned int events[6];
static unsigned int count;
static uint16_t teardown_arg;
static uint16_t exit_arg;

static void record(unsigned int event)
{
    assert(count < 6u);
    events[count++] = event;
}

void gate_linked_shutdown_intro_stream(void) { record(1u); }
void gate_linked_shutdown_audio(void) { record(2u); }
void gate_linked_vfx_shutdown(void) { record(3u); }
void gate_rtl_clear_font_cache(void) { record(4u); }
void gate_rtl_control_teardown(uint16_t reset_display)
{
    teardown_arg = reset_display;
    record(5u);
}
void gate_linked_crt_exit(uint16_t status)
{
    exit_arg = status;
    record(6u);
}

int main(void)
{
    unsigned int i;
    gate_linked_handle_control_key();
    assert(count == 6u);
    for (i = 0u; i < 6u; ++i)
        assert(events[i] == i + 1u);
    assert(teardown_arg == 1u);
    assert(exit_arg == 1u);
    return 0;
}
