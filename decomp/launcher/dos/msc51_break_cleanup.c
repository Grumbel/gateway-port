/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

/* GATE.RTL:11e5f..11ec5, fifth FAR callee of the original Ctrl-Break
 * handler at RTL:157b8. This is the original control-flow ordering, not
 * the still-unrecovered OUTER control-key handler or DOS process exit.
 *
 * Unlike the original, reject uninitialized saved vectors, selectors and
 * standard DOS handles before modifying the IVT. The original 500ms RAM
 * capture has zero saved vectors: running its cleanup before the original
 * startup installed handlers would corrupt INT 1Bh/23h. */
#ifdef GATE_BREAK_CLEANUP_HOST_TEST
#include "../gate_exe.h"
#else
#include "GATEMEM.H"
#include "RTLLAY.H"
#endif
#include <stdlib.h>

void gate_linked_dos_setvect(uint16_t number, GateFarPtr handler);
void gate_rtl_close_resource_file(uint16_t handle);
uint16_t gate_rtl_mode_start(uint16_t enabled);

static GateFarPtr saved_interrupt_vector(uint16_t offset)
{
    GateFarPtr result;
    result.offset = gate_ds_word(offset);
    result.segment = gate_ds_word((uint16_t)(offset + 2u));
    return result;
}

static int vector_is_nonnull(GateFarPtr address)
{
    return address.offset != 0u || address.segment != 0u;
}

void gate_rtl_control_teardown(uint16_t reset_display)
{
    uint16_t mode_segment = gate_ds_word(GATE_DS_IMAGE_MODE_SEGMENT);
    uint16_t resource_segment = gate_ds_word(GATE_DS_RESOURCE_HANDLE_SEGMENT);
    uint16_t resource_handle;
    uint16_t mode;
    GateFarPtr timer, break_handler, control_handler;

    if (mode_segment == 0u || resource_segment == 0u)
        abort();
    mode = gate_far_word(mode_segment, GATE_DS_GRAPHICS_MODE);
    resource_handle = gate_far_word(resource_segment, 0x005au);
    timer = saved_interrupt_vector(GATE_DS_BREAK_INT1C_OFFSET);
    break_handler = saved_interrupt_vector(GATE_DS_BREAK_INT1B_OFFSET);
    control_handler = saved_interrupt_vector(GATE_DS_BREAK_INT23_OFFSET);

    /* A resource file must not close STDIN/OUT/ERR by accident. The
     * original code has no such validation; this is a bounded subset. */
    if (resource_handle < 3u || !vector_is_nonnull(break_handler) ||
        !vector_is_nonnull(control_handler) ||
        (mode != 0u && !vector_is_nonnull(timer)))
        abort();

    if (mode != 0u)
        gate_linked_dos_setvect(0x1cu, timer);
    gate_linked_dos_setvect(0x1bu, break_handler);
    gate_linked_dos_setvect(0x23u, control_handler);
    gate_rtl_close_resource_file(resource_handle);
    if (reset_display != 0u)
        (void)gate_rtl_mode_start(0u);
}
