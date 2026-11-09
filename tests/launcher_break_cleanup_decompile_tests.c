/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Independent host oracle for RTL:11e5f. DOSBox checks the actual IVT,
 * DOS handle release and mode-3 transition in a separate probe. */
#include "gate_exe.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

void gate_rtl_control_teardown(uint16_t reset_display);

static uint16_t words[0x10000];
static uint16_t mode_word;
static uint16_t resource_word;
static uint16_t observed_numbers[5];
static GateFarPtr observed_handlers[5];
static int events[5];
static unsigned int event_count;

uint16_t gate_ds_word(uint16_t offset) { return words[offset]; }
uint16_t gate_far_word(uint16_t segment, uint16_t offset)
{
    if (segment == 0x9000u && offset == GATE_DS_GRAPHICS_MODE)
        return mode_word;
    if (segment == 0x9001u && offset == 0x005au)
        return resource_word;
    abort();
}

void gate_linked_dos_setvect(uint16_t number, GateFarPtr handler)
{
    assert(event_count < 5u);
    events[event_count] = 1;
    observed_numbers[event_count] = number;
    observed_handlers[event_count] = handler;
    ++event_count;
}

void gate_rtl_close_resource_file(uint16_t handle)
{
    assert(event_count < 5u);
    events[event_count] = 2;
    observed_numbers[event_count] = handle;
    ++event_count;
}

uint16_t gate_rtl_mode_start(uint16_t enabled)
{
    assert(event_count < 5u);
    events[event_count] = 3;
    observed_numbers[event_count] = enabled;
    ++event_count;
    return 0u;
}

static void initialize(uint16_t mode)
{
    memset(words, 0, sizeof words);
    memset(events, 0, sizeof events);
    event_count = 0u;
    words[GATE_DS_IMAGE_MODE_SEGMENT] = 0x9000u;
    words[GATE_DS_RESOURCE_HANDLE_SEGMENT] = 0x9001u;
    words[GATE_DS_BREAK_INT1C_OFFSET] = 0x0100u;
    words[GATE_DS_BREAK_INT1C_SEGMENT] = 0x1111u;
    words[GATE_DS_BREAK_INT1B_OFFSET] = 0x0200u;
    words[GATE_DS_BREAK_INT1B_SEGMENT] = 0x2222u;
    words[GATE_DS_BREAK_INT23_OFFSET] = 0x0300u;
    words[GATE_DS_BREAK_INT23_SEGMENT] = 0x3333u;
    mode_word = mode;
    resource_word = 7u;
}

static void check_event(unsigned int position, int type, uint16_t argument)
{
    assert(events[position] == type);
    assert(observed_numbers[position] == argument);
}

#ifndef _WIN32
static void expect_abort(void)
{
    int status;
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        struct rlimit limit = {0, 0};
        (void)setrlimit(RLIMIT_CORE, &limit);
        gate_rtl_control_teardown(1u);
        _exit(99);
    }
    assert(waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status));
    assert(WTERMSIG(status) == SIGABRT);
}
#endif

int main(void)
{
    initialize(2u);
    gate_rtl_control_teardown(1u);
    assert(event_count == 5u);
    check_event(0u, 1, 0x1cu);
    check_event(1u, 1, 0x1bu);
    check_event(2u, 1, 0x23u);
    check_event(3u, 2, 7u);
    check_event(4u, 3, 0u);
    assert(observed_handlers[0].segment == 0x1111u);
    assert(observed_handlers[0].offset == 0x0100u);
    assert(observed_handlers[1].segment == 0x2222u);
    assert(observed_handlers[1].offset == 0x0200u);
    assert(observed_handlers[2].segment == 0x3333u);
    assert(observed_handlers[2].offset == 0x0300u);

    /* Mode zero skips INT 1Ch, and argument zero skips video shutdown. */
    initialize(0u);
    words[GATE_DS_BREAK_INT1C_OFFSET] = 0u;
    words[GATE_DS_BREAK_INT1C_SEGMENT] = 0u;
    gate_rtl_control_teardown(0u);
    assert(event_count == 3u);
    check_event(0u, 1, 0x1bu);
    check_event(1u, 1, 0x23u);
    check_event(2u, 2, 7u);

#ifndef _WIN32
    initialize(2u);
    words[GATE_DS_BREAK_INT1C_OFFSET] = 0u;
    words[GATE_DS_BREAK_INT1C_SEGMENT] = 0u;
    expect_abort();
    initialize(0u);
    words[GATE_DS_BREAK_INT23_OFFSET] = 0u;
    words[GATE_DS_BREAK_INT23_SEGMENT] = 0u;
    expect_abort();
    initialize(0u);
    resource_word = 1u;
    expect_abort();
    initialize(0u);
    words[GATE_DS_RESOURCE_HANDLE_SEGMENT] = 0u;
    expect_abort();
#endif
    return 0;
}
