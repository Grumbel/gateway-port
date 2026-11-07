/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifdef NDEBUG
#undef NDEBUG
#endif

#include "gate_rtl_palette.h"
#include "gate_intro_host_memory.h"
#include "gate_intro_host_vga_ports.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const GateFarPtr palette = {0x4000, 0x0100};

static void palette_complete(void *userdata)
{
    ++*(unsigned *)userdata;
}

static void install_palette(uint16_t count)
{
    uint16_t i;
    for (i = 0; i < count; ++i)
        gate_far_set_byte(palette.segment, (uint16_t)(palette.offset + i),
                          (uint8_t)(1 + i % 63));
}

static void check_direct_dac(GateIntroVgaPorts *ports, uint16_t mode)
{
    uint16_t colors = mode == 3 ? 256 : 16;
    uint16_t i;
    unsigned complete = 0;
    memset(ports, 0, sizeof *ports);
    ports->palette_ready = palette_complete;
    ports->palette_userdata = &complete;
    gate_ds_set_word(0x0fce, mode);
    install_palette((uint16_t)(colors * 3));
    gate_rtl_f61b(palette); /* original source-C palette uploader */
    assert(ports->status_reads >= 2);
    assert(ports->dac_writes == colors * 3);
    assert(complete == 1); /* 16 triples in mode 12h, 256 in mode 13h */
    for (i = 0; i < colors; ++i) {
        unsigned c;
        for (c = 0; c < 3; ++c) {
            uint16_t offset = (uint16_t)(3 * i + c);
            uint8_t sample = gate_far_byte(palette.segment,
                                            (uint16_t)(palette.offset + offset));
            assert(ports->dac[i][c] == (sample & 0x3fu));
        }
    }
    if (mode == 2) {
        assert(ports->display_enabled);
        assert(ports->attributes[0x11] == 15); /* first RGB byte nonzero */
    } else {
        assert(!ports->display_enabled); /* mode 3 does not touch 3c0 */
    }
}

static void check_other_modes(GateIntroVgaPorts *ports)
{
    uint32_t reads, writes;
    /* Mode 1 uses packed 2-bit RGB components expanded to VGA DAC 6-bit.
     * The original source always uploads an attribute-controller command. */
    memset(ports, 0, sizeof *ports);
    gate_ds_set_word(0x0fce, 1);
    for (uint16_t i = 0; i < 16; ++i)
        gate_far_set_byte(palette.segment, (uint16_t)(palette.offset + i),
                          (uint8_t)(i == 15 ? 63 : i));
    gate_rtl_f61b(palette);
    assert(ports->dac_writes == 16 * 3);
    assert(ports->dac[0][0] == 0);
    assert(ports->dac[15][0] == 63 && ports->dac[15][1] == 63 &&
           ports->dac[15][2] == 63);
    assert(ports->display_enabled);

    /* Conversion != 0 uses the original two relocated 10-byte lookup
     * arrays. Mode 3 uses only the first signed byte of each RGB triple
     * and divides it by 31 before multiplying the lookup index by three. */
    memset(ports, 0, sizeof *ports);
    gate_far_set_word(0x2a70, 0x40, 1);
    for (uint16_t i = 0; i < 10; ++i) {
        gate_far_set_byte(0x2a7a, (uint16_t)(0x132a + i), (uint8_t)(10 + i));
        gate_far_set_byte(0x2a7a, (uint16_t)(0x1334 + i), (uint8_t)(20 + i));
    }
    assert(gate_rtl_palette_table_byte(0x35c) == 10);
    assert(gate_rtl_palette_table_byte(0x366) == 20);
    gate_ds_set_word(0x0fce, 3);
    install_palette(256 * 3);
    gate_rtl_f61b(palette);
    assert(ports->dac_writes == 256 * 3);
    for (uint16_t i = 0; i < 256; ++i) {
        const uint8_t sample = gate_far_byte(palette.segment,
                                              (uint16_t)(palette.offset + 3 * i));
        const uint16_t index = (uint16_t)((int8_t)sample / 31 * 3);
        assert(ports->dac[i][0] == 10 + index);
        assert(ports->dac[i][1] == 20 + index);
        assert(ports->dac[i][2] == 0);
    }
    gate_far_set_word(0x2a70, 0x40, 0);

    /* Source C exits *before* the retrace wait for graphics mode zero. */
    reads = ports->status_reads;
    writes = ports->dac_writes;
    gate_ds_set_word(0x0fce, 0);
    gate_rtl_f61b(palette);
    assert(ports->status_reads == reads && ports->dac_writes == writes);
}

static void initialize_globals(void)
{
    gate_ds_set_word(0x5514, 0x03da); /* original input status port */
    gate_ds_set_word(0x5516, 5);      /* VGA card type */
    gate_ds_set_word(0x0fce + 6, 16);
    gate_ds_set_word(0x0fce + 8, 48);
    gate_ds_set_word(0x0fce + 10, 768);
    gate_ds_set_word(0x4daa, 0x2a70); /* mode's conversion flag segment */
    gate_far_set_word(0x2a70, 0x40, 0);
    assert(gate_rtl_palette_status_port() == 0x3da);
    assert(gate_rtl_palette_video_type() == 5);
    assert(gate_rtl_palette_conversion() == 0);
    assert(gate_rtl_palette_color_count(10) == 768);
    assert(gate_rtl_palette_color_count(8) == 48);
}

static void check_original_snapshot(const char *path)
{
    GateIntroMemory *memory = calloc(1, sizeof *memory);
    GateIntroVgaPorts *ports = calloc(1, sizeof *ports);
    FILE *file;
    assert(memory && ports);
    file = fopen(path, "rb");
    assert(file);
    assert(fread(memory->bytes, 1, sizeof memory->bytes, file) ==
           sizeof memory->bytes);
    assert(fclose(file) == 0);
    memory->ds_segment = 0x2a7a; /* original 22-second guest DS */
    gate_intro_memory_attach(memory);
    gate_intro_vga_ports_attach(ports);
    assert(gate_rtl_palette_mode() == 3);
    assert(gate_rtl_palette_video_type() == 5);
    assert(gate_rtl_palette_status_port() == 0x3da);
    assert(gate_rtl_palette_color_count(10) == 768);
    assert(gate_rtl_palette_conversion() == 0);
    check_direct_dac(ports, 3);
    check_direct_dac(ports, 2);
    gate_intro_vga_ports_detach();
    gate_intro_memory_detach();
    free(ports);
    free(memory);
}

int main(int argc, char **argv)
{
    GateIntroMemory *memory = calloc(1, sizeof *memory);
    GateIntroVgaPorts *ports = calloc(1, sizeof *ports);
    assert(memory && ports);
    memory->ds_segment = 0x2a7a;
    gate_intro_memory_attach(memory);
    gate_intro_vga_ports_attach(ports);
    initialize_globals();
    check_direct_dac(ports, 3);
    check_direct_dac(ports, 2);
    check_other_modes(ports);
    gate_intro_vga_ports_detach();
    gate_intro_memory_detach();
    free(ports);
    free(memory);
    if (argc > 1)
        check_original_snapshot(argv[1]);
    puts("original RTL palette to native VGA DAC and attributes: OK");
    return 0;
}
