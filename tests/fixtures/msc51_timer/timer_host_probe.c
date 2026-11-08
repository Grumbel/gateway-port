/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "GATEMEM.H"
#include <dos.h>
#include <stdio.h>

int gate_dos_probe_interrupt_1c(uint16_t *observed);

static uint32_t clock_ms;
static unsigned int vector_installs;
static unsigned int inject_ticks;
static void legacy_handler(void) {}
static void (*current_handler)(void) = legacy_handler;

void (*_dos_getvect(unsigned int vector))(void)
{
    return vector == 0x1cu ? current_handler : (void (*)(void))0;
}

void _dos_setvect(unsigned int vector, void (*handler)(void))
{
    if (vector == 0x1cu) {
        ++vector_installs;
        current_handler = handler;
    }
}

int int86(int number, union REGS *input, union REGS *output)
{
    uint32_t secs;
    if (number != 0x21 || input->h.ah != 0x2cu)
        return -1;
    clock_ms += 10ul;
    if (inject_ticks && (clock_ms % 50ul) == 0ul)
        current_handler();
    secs = clock_ms / 1000ul;
    output->h.ch = (unsigned char)(secs / 3600ul);
    output->h.cl = (unsigned char)((secs / 60ul) % 60ul);
    output->h.dh = (unsigned char)(secs % 60ul);
    output->h.dl = (unsigned char)((clock_ms % 1000ul) / 10ul);
    return 0;
}

int main(void)
{
    uint16_t count = 0;
    int rc;
    inject_ticks = 1u;
    rc = gate_dos_probe_interrupt_1c(&count);
    if (rc != 0 || count < 2u || current_handler != legacy_handler ||
        vector_installs != 2u) {
        fprintf(stderr, "INT 1Ch host success path failed: %d %u\n", rc, count);
        return 1;
    }
    inject_ticks = 0u;
    rc = gate_dos_probe_interrupt_1c(&count);
    if (rc != -1 || count != 0u || current_handler != legacy_handler ||
        vector_installs != 4u) {
        fprintf(stderr, "INT 1Ch host timeout path failed: %d %u\n", rc, count);
        return 1;
    }
    puts("INT 1Ch host callback/timeout/restoration: PASS");
    return 0;
}
