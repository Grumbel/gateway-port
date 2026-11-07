/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "STDINT.H"
#include <stdio.h>

uint32_t gate_linked_exe_elapsed_milliseconds(void);
uint32_t gate_linked_animation_elapsed_milliseconds(void);
void gate_linked_wait_animation_delay(uint32_t delay);
void gate_rtl_graphics_delay(uint32_t delay);

int main(void)
{
    uint32_t first = gate_linked_exe_elapsed_milliseconds();
    uint32_t second = gate_linked_animation_elapsed_milliseconds();
    uint32_t end;
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
