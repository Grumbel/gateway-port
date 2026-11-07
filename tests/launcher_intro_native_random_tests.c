/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifdef NDEBUG
#undef NDEBUG
#endif

#include "gate_intro_host_memory.h"
#include "gate_rtl_image.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t delays[4];
static unsigned delay_count, draw_count, restore_count;

uint16_t gate_rtl_resource_ds_word(uint16_t offset)
{
    assert(offset == 2 || offset == 4);
    return 0;
}
uint16_t gate_linked_save_vfx_draw_state(void) { return 0x5531; }
void gate_linked_restore_vfx_draw_state(uint16_t saved)
{
    assert(saved == 0x5531);
    ++restore_count;
}
void gate_linked_blit_vfx_picture(uint16_t zero, uint16_t y, uint16_t x,
                        uint16_t signed_mode, GateFarPtr pixels)
{
    assert(zero == 0 && y == 7 && x == 11 && signed_mode == 2);
    assert(gate_far_byte(pixels.segment, pixels.offset) == 0);
    ++draw_count;
}
void gate_linked_wait_animation_delay(uint32_t delay)
{
    assert(delay_count < sizeof delays / sizeof delays[0]);
    delays[delay_count++] = delay;
}

static void check_sequence(void)
{
    static const uint16_t expected[] = {41, 18467, 6334, 26500};
    static const uint32_t expected_states[] = {
        0x0029e2c0u, 0xc823f683u, 0x18be873au, 0xe7847115u
    };
    unsigned i;
    gate_linked_seed_runtime_random(1);
    assert(gate_ds_word(0x358e) == 1 && gate_ds_word(0x3590) == 0);
    for (i = 0; i < 4; ++i) {
        uint32_t state;
        assert(gate_linked_runtime_random() == expected[i]);
        state = (uint32_t)gate_ds_word(0x358e) |
                ((uint32_t)gate_ds_word(0x3590) << 16);
        assert(state == expected_states[i]);
    }

    gate_linked_seed_runtime_random(0);
    assert(gate_linked_runtime_random() == 38);
    assert(gate_ds_word(0x358e) == 0x9ec3);
    assert(gate_ds_word(0x3590) == 0x0026);
    gate_linked_seed_runtime_random(0xffff);
    assert(gate_linked_runtime_random() == 17440);
    assert(gate_ds_word(0x358e) == 0x5ac6);
    assert(gate_ds_word(0x3590) == 0x4420);

    /* The seed state may contain any 32-bit value after an earlier rand;
     * the multiplication must discard its upper 32 bits. */
    gate_ds_set_word(0x358e, 0xffff);
    gate_ds_set_word(0x3590, 0xffff);
    assert(gate_linked_runtime_random() == 35);
    assert(gate_ds_word(0x358e) == 0x5ac6);
    assert(gate_ds_word(0x3590) == 0x0023);
}

static void check_original_frame_delays(void)
{
    uint8_t records[0x110] = {0};
    GateFarPtr work;
    gate_linked_seed_runtime_random(1);
    delay_count = draw_count = restore_count = 0;
    records[0] = 1;
    records[1] = 2;
    records[2] = 11;
    records[4] = 7;
    records[6] = 3;
    records[7] = 4;
    records[0x88] = 1;
    records[0x89] = 2;
    records[0x8a] = 11;
    records[0x8c] = 7;
    records[0x8e] = 2;
    records[0x8f] = 1;

    work = gate_stack_pointer(records);
    gate_rtl_play_timed_picture_records(work, 2);
    assert(draw_count == 2 && restore_count == 2 && delay_count == 2);
    /* The original image source C computes delay from its two header bytes
     * and the *real* compiler-library rand(), not a canned clock value. */
    assert(delays[0] == 150u + (41u % 201u));
    assert(delays[1] == 100u + (18467u % 51u));
    assert(gate_ds_word(0x358e) == 0xf683);
    assert(gate_ds_word(0x3590) == 0xc823);
}

static void check_original_capture(GateIntroMemory *memory, const char *path)
{
    FILE *in = fopen(path, "rb");
    assert(in);
    assert(fread(memory->bytes, 1, GATE_INTRO_RAM_BYTES, in) ==
           GATE_INTRO_RAM_BYTES);
    assert(fgetc(in) == EOF);
    fclose(in);
    memory->ds_segment = 0x2a7a;
    assert(gate_ds_word(0x358e) == 0x1c7b);
    assert(gate_ds_word(0x3590) == 0x305e);
    assert(gate_linked_runtime_random() == 17421);
    assert(gate_ds_word(0x358e) == 0xf552);
    assert(gate_ds_word(0x3590) == 0xc40d);
}

int main(int argc, char **argv)
{
    GateIntroMemory *memory = calloc(1, sizeof *memory);
    assert(memory);
    memory->ds_segment = 0x3000;
    gate_intro_memory_attach(memory);
    check_sequence();
    check_original_frame_delays();
    if (argc > 1)
        check_original_capture(memory, argv[1]);
    gate_intro_memory_detach();
    free(memory);
    puts("original C runtime rand and source-C animation delays: OK");
    return 0;
}
