/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "GATEMEM.H"
#include <dos.h>
#include <conio.h>
#include <stdio.h>

int gate_dos_probe_interrupt_1c(uint16_t *observed);
int gate_dos_probe_pit_calibration(uint16_t *iterations_per_tick,
                                   uint16_t *observed);

static uint32_t clock_ms;
static unsigned int vector_installs;
static unsigned int inject_ticks;
static unsigned int pit_write_count;
static unsigned int pit_write_ports[6];
static unsigned int pit_write_values[6];
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

int outp(unsigned int port, int value)
{
    if (pit_write_count < 6u) {
        pit_write_ports[pit_write_count] = port;
        pit_write_values[pit_write_count] = (unsigned int)value;
    }
    ++pit_write_count;
    return value;
}

static int pit_write_sequence_valid(void)
{
    static const unsigned int ports[] = {0x43u, 0x40u, 0x40u, 0x43u, 0x40u, 0x40u};
    static const unsigned int values[] = {0x34u, 0xaeu, 0x4du, 0x34u, 0xffu, 0xffu};
    unsigned int i;
    if (pit_write_count != 6u)
        return 0;
    for (i = 0; i < 6u; ++i)
        if (pit_write_ports[i] != ports[i] || pit_write_values[i] != values[i])
            return 0;
    return 1;
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
    uint16_t iterations = 0;
    int rc;
    inject_ticks = 1u;
    rc = gate_dos_probe_interrupt_1c(&count);
    if (rc != 0 || count < 2u || current_handler != legacy_handler ||
        vector_installs != 2u) {
        fprintf(stderr, "INT 1Ch host success path failed: %d %u\n", rc, count);
        return 1;
    }
    pit_write_count = 0u;
    rc = gate_dos_probe_pit_calibration(&iterations, &count);
    if (rc != 0 || count < 8u || iterations == 0u ||
        current_handler != legacy_handler || vector_installs != 4u ||
        !pit_write_sequence_valid()) {
        fprintf(stderr, "PIT 60Hz host success path failed: %d %u %u\n",
                rc, count, iterations);
        return 1;
    }
    inject_ticks = 0u;
    rc = gate_dos_probe_interrupt_1c(&count);
    if (rc != -1 || count != 0u || current_handler != legacy_handler ||
        vector_installs != 6u) {
        fprintf(stderr, "INT 1Ch host timeout path failed: %d %u\n", rc, count);
        return 1;
    }
    pit_write_count = 0u;
    rc = gate_dos_probe_pit_calibration(&iterations, &count);
    if (rc != -1 || count != 0u || current_handler != legacy_handler ||
        vector_installs != 8u || !pit_write_sequence_valid()) {
        fprintf(stderr, "PIT 60Hz host timeout path failed: %d %u\n", rc, count);
        return 1;
    }
    puts("INT 1Ch host callback/timeout/restoration and PIT writes: PASS");
    return 0;
}
