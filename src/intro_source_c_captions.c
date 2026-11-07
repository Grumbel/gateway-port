/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "intro_source_c_captions.h"
#include "gate_exe.h"
#include "gate_dos_host_files.h"
#include "gate_intro_host_input.h"
#include "gate_intro_host_memory.h"
#include "gate_rtl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct CaptionCall {
    uint16_t left, top, right, text_offset;
} CaptionCall;

/* Exactly the EXE:1302 calls, not copied narrative text. The full program
 * interleaves these with images, fades and animations not modeled here. */
static const CaptionCall captions[] = {
    {10, 120, 310, 0x363b}, {10, 100, 180, 0x37bf},
    {10,  10, 160, 0x3899}, {10,  10, 310, 0x395a},
    {160, 10, 310, 0x3a65}, {180, 10, 310, 0x3b9b},
    {180, 10, 310, 0x3c4f}, {130, 10, 310, 0x3d71},
    {140, 10, 310, 0x3e79}, {140, 10, 310, 0x3f3b},
    {140, 10, 310, 0x4086}, {10,  10, 140, 0x4198},
    {60, 120, 310, 0x42ae}, {140, 120, 310, 0x436c},
    {120, 10, 310, 0x4436}, {10, 100, 160, 0x45c4},
    {10,  10, 310, 0x46c5}, {10,  10, 310, 0x4820}
};

struct GateSourceCCaptions {
    GateIntroMemory *memory;
    GateDosHostFiles files;
    GateIntroHostInput input;
    uint32_t milliseconds;
    uint32_t duration;
};

static uint32_t sample_clock(void *context)
{
    GateSourceCCaptions *player = context;
    player->milliseconds += 50;
    return player->milliseconds;
}

static uint16_t no_input(void *context)
{
    (void)context;
    return 0;
}

static uint16_t unexpected_read(void *context)
{
    (void)context;
    abort();
}

/* The original RTL text-mode branches are not reachable with the captured
 * 320x200 graphics configuration. Fail explicitly if that assumption breaks. */
void gate_rtl_set_text_cell_cursor(uint16_t x, uint16_t y)
{
    (void)x;
    (void)y;
    abort();
}
void gate_rtl_print_text_mode_string(GateFarPtr text) { (void)text; abort(); }
void gate_rtl_get_text_cell_cursor(uint16_t *x, uint16_t *y)
{
    (void)x;
    (void)y;
    abort();
}
uint16_t gate_rtl_file_stack_word(void) { return 0; }
_Noreturn void gate_rtl_file_divide_fault(void) { abort(); }

static int configure(GateIntroMemory *memory)
{
    unsigned i;
    if (gate_ds_word(GATE_DS_GRAPHICS_MODE) != 3 || gate_ds_word(GATE_DS_RENDERER_MODE) != 3)
        return 0;
    for (i = 0; i < 2; ++i) {
        uint16_t slot = (uint16_t)(0x54ce + i * 30u);
        gate_ds_set_word(slot, 0);
        gate_far_set_byte(memory->ds_segment, (uint16_t)(slot + 2u), 0xff);
        gate_far_set_byte(memory->ds_segment, (uint16_t)(slot + 3u), 0xff);
    }
    memset(memory->bytes + 0xa0000, 0, 320u * 200u);
    gate_ds_set_word(GATE_DS_DRAW_OPERATION, 0);
    gate_ds_set_word(GATE_DS_DRAW_CLIPPING_ENABLED, 1);
    gate_ds_set_word(GATE_DS_DRAW_CLIP_LEFT, 0);
    gate_ds_set_word(GATE_DS_DRAW_CLIP_TOP, 0);
    gate_ds_set_word(GATE_DS_DRAW_CLIP_RIGHT, 319);
    gate_ds_set_word(GATE_DS_DRAW_CLIP_BOTTOM, 199);
    gate_ds_set_word(GATE_DS_DRAW_LINE_MASK, 0xffffu);
    gate_ds_set_word(GATE_DS_DRAW_LINE_WIDTH, 1);
    gate_ds_set_word(GATE_DS_FILL_PATTERN, 0);
    gate_ds_set_word(GATE_DS_FILL_PATTERN_ACTIVE, 0);
    return 1;
}

GateSourceCCaptions *gate_source_c_captions_open(const char *image,
                                                  const char *directory)
{
    GateSourceCCaptions *player;
    FILE *file;
    if (!image || !directory)
        return NULL;
    player = calloc(1, sizeof *player);
    if (!player)
        return NULL;
    player->memory = calloc(1, sizeof *player->memory);
    if (!player->memory) {
        free(player);
        return NULL;
    }
    file = fopen(image, "rb");
    if (!file || fread(player->memory->bytes, 1, GATE_INTRO_RAM_BYTES, file) !=
                 GATE_INTRO_RAM_BYTES) {
        if (file)
            fclose(file);
        free(player->memory);
        free(player);
        return NULL;
    }
    fclose(file);
    player->memory->ds_segment = 0x2a7a; /* verified 22-second capture */
    player->files.directory = directory;
    player->input.userdata = player;
    player->input.clock_ms = sample_clock;
    player->input.key_pending = no_input;
    player->input.read_key = unexpected_read;
    player->input.mouse_flags = no_input;
    gate_intro_memory_attach(player->memory);
    gate_dos_host_files_attach(&player->files);
    gate_intro_input_attach(&player->input);
    if (!configure(player->memory)) {
        gate_intro_input_detach();
        gate_dos_host_files_close_all(&player->files);
        gate_intro_memory_detach();
        free(player->memory);
        free(player);
        return NULL;
    }
    return player;
}

void gate_source_c_captions_close(GateSourceCCaptions *player)
{
    if (!player)
        return;
    gate_rtl_clear_font_cache(); /* original font-cache unload */
    gate_intro_input_detach();
    gate_dos_host_files_close_all(&player->files);
    gate_intro_memory_detach();
    free(player->memory);
    free(player);
}

unsigned gate_source_c_captions_count(void)
{
    return (unsigned)(sizeof captions / sizeof captions[0]);
}

int gate_source_c_captions_render(GateSourceCCaptions *player, unsigned page)
{
    const CaptionCall *call;
    GateFarPtr text;
    uint32_t before;
    if (!player || page >= gate_source_c_captions_count())
        return 0;
    call = &captions[page];
    text.segment = player->memory->ds_segment;
    text.offset = call->text_offset;
    memset(player->memory->bytes + 0xa0000, 0, 320u * 200u);
    before = player->milliseconds;
    gate_exe_show_caption(call->left, call->top, call->right, text);
    player->duration = player->milliseconds - before;
    return 1;
}

const uint8_t *gate_source_c_captions_pixels(const GateSourceCCaptions *player)
{
    return player ? player->memory->bytes + 0xa0000 : NULL;
}

uint32_t gate_source_c_captions_duration(const GateSourceCCaptions *player)
{
    return player ? player->duration : 0;
}
