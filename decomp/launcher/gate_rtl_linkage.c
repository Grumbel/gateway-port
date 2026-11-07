/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_exe.h"
#include "gate_rtl.h"
#include "gate_rtl_graphics.h"
#include "gate_rtl_palette.h"
#include "gate_rtl_mode.h"
#include "gate_rtl_draw.h"
#include "gate_rtl_image.h"
#include "gate_rtl_video.h"

/* Source-level linker aliases, not additional recovered original routines.
 * The original EXE calls fixed linked segment:offset addresses; the
 * GATE.RTL file offsets below were verified against captured DOS memory.
 * Keeping this bridge separate preserves the original caller source and
 * allows individual C test targets to use their original-call mock oracles. */

uint16_t gate_linked_font_line_advance(void) { return gate_rtl_font_line_advance(); }
void gate_linked_select_intro_font(uint16_t request) { (void)gate_rtl_select_font(request); }
uint16_t gate_linked_font_character_advance(uint16_t ch) { return gate_rtl_font_character_advance(ch); }
uint16_t gate_linked_measure_text_width(GateFarPtr string) { return gate_rtl_measure_text_width(string); }
void gate_linked_position_intro_text(int16_t x, int16_t y) { gate_rtl_set_text_cursor(x, y); }
void gate_linked_draw_intro_text(GateFarPtr string) { gate_rtl_draw_text_string(string); }
void gate_linked_set_intro_text_colors(uint16_t ink, uint16_t background)
{
    gate_rtl_set_text_colors(ink, (int16_t)background);
}

void gate_linked_fade_intro_palette_in(void) { gate_rtl_fade_palette_in(); }
void gate_linked_clear_intro_palette(void) { gate_rtl_clear_palette_buffer(); }
void gate_linked_fade_intro_palette_out(void) { gate_rtl_fade_palette_out(); }
void gate_linked_initialize_intro_picture_record(GateFarPtr work)
{
    gate_rtl_initialize_picture_record(work);
}
void gate_linked_release_intro_picture_record(GateFarPtr work) { gate_rtl_release_picture_record(work); }

void gate_linked_upload_intro_palette(GateFarPtr palette) { gate_rtl_upload_vga_palette(palette); }
void gate_linked_clear_intro_display(void) { gate_rtl_clear_video_display(); }
void gate_linked_set_intro_video_mode(uint16_t mode) { (void)gate_rtl_set_video_mode((int16_t)mode); }
void gate_linked_set_intro_pen_color(uint16_t color) { (void)gate_rtl_set_pen_color(color); }
void gate_linked_fill_intro_rectangle(uint16_t style, uint16_t left, uint16_t top,
                         uint16_t right, uint16_t bottom)
{
    gate_rtl_fill_rectangle(style, left, top, right, bottom);
}
void gate_linked_draw_intro_caption_frame(uint16_t left, uint16_t top, uint16_t right,
                         uint16_t bottom, uint16_t style)
{
    gate_rtl_draw_caption_frame(left, top, right, bottom, style);
}

/* The original graphics code passes SS:offset of its local palette array,
 * while host C represents that stack storage as a native pointer. Keep
 * this source-level FAR-pointer translation at the link boundary. */
void gate_rtl_graphics_palette_upload(const uint8_t *palette)
{
    GateFarPtr original_pointer = palette != 0
        ? gate_stack_pointer((void *)palette)
        : (GateFarPtr){0, 0};
    gate_rtl_upload_vga_palette(original_pointer);
}

void gate_linked_show_intro_picture(uint16_t resource, uint16_t mode)
{
    (void)gate_rtl_show_picture(resource, mode);
}

void gate_linked_load_intro_picture_record(GateFarPtr work, uint16_t resource, uint16_t mode)
{
    (void)gate_rtl_load_picture_record(work, resource, mode);
}
void gate_linked_draw_intro_picture_record(GateFarPtr work) { gate_rtl_draw_picture_record(work); }
void gate_linked_play_intro_delta_sequence(uint16_t first, uint16_t next, uint16_t count)
{
    gate_rtl_play_picture_delta_sequence(first, next, (int16_t)count);
}
void gate_linked_play_intro_preloaded_sequence(uint16_t first, uint16_t count)
{
    gate_rtl_play_preloaded_picture_sequence(first, (int16_t)count);
}
