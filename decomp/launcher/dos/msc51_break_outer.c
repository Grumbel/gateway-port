/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

/* GATE.RTL:157b8..157e4, observed live at 123f:0004. Six FAR calls in
 * original order; each site was verified against the 22-second snapshot.
 * The final RTL:621e CRT termination is a separate DOS adapter: this
 * translation does not claim its process-wide atexit/IRQ teardown code. */
#ifdef GATE_BREAK_OUTER_HOST_TEST
#include "../gate_exe.h"
#else
#include "GATEMEM.H"
#endif

void gate_linked_shutdown_intro_stream(void); /* RTL:15706 */
void gate_linked_shutdown_audio(void);        /* RTL:c391 */
void gate_linked_vfx_shutdown(void);          /* RTL:10da1 */
void gate_rtl_clear_font_cache(void);         /* RTL:e771 */
void gate_rtl_control_teardown(uint16_t reset_display); /* RTL:11e5f */
void gate_linked_crt_exit(uint16_t status);   /* RTL:621e, adapter only */

void gate_linked_handle_control_key(void)
{
    gate_linked_shutdown_intro_stream();
    gate_linked_shutdown_audio();
    gate_linked_vfx_shutdown();
    gate_rtl_clear_font_cache();
    gate_rtl_control_teardown(1u);
    gate_linked_crt_exit(1u);
}
