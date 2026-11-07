/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* DOS probe declarations for RTL state accessors. All implementations are
 * staged from src/intro_host_rtl_*_globals.c, not duplicated. */
#ifndef GATE_MSC51_RTL_STATE_H
#define GATE_MSC51_RTL_STATE_H

#include "GATEMEM.H"

uint16_t gate_rtl_image_mode_segment(void);
uint16_t gate_rtl_image_palette_segment(void);
uint16_t gate_rtl_preparer_flag_source_segment(void);
uint16_t gate_rtl_preparer_status_segment(void);
uint16_t gate_rtl_resource_handle_segment(void);
uint16_t gate_rtl_image_header_segment(void);
uint16_t gate_rtl_resource_size_table_segment(void);
uint16_t gate_rtl_image_transform_segment(void);
uint16_t gate_rtl_preparer_word_dest_segment(void);
uint16_t gate_rtl_anim_segment_22(void);
uint16_t gate_rtl_anim_segment_24(void);
uint16_t gate_rtl_anim_segment_26(void);
uint16_t gate_rtl_image_data_segment(void);
uint8_t gate_rtl_image_data_byte(uint16_t offset);
uint16_t gate_rtl_resource_ds_word(uint16_t offset);
void gate_rtl_resource_ds_set_word(uint16_t offset, uint16_t value);
GateFarPtr gate_rtl_image_palette_source(void);

GateFarPtr gate_rtl_active_font(void);
int16_t gate_rtl_cursor_x(void);
int16_t gate_rtl_cursor_y(void);
void gate_rtl_set_cursor(int16_t x, int16_t y);
uint16_t gate_rtl_ink_color(void);
int16_t gate_rtl_background_color(void);
void gate_rtl_set_colors(uint16_t ink, int16_t background);
uint16_t gate_rtl_tab_advance(void);
int16_t gate_rtl_text_mode_pitch(void);
int16_t gate_rtl_tab_pitch(void);
int16_t gate_rtl_tab_limit(void);
int16_t gate_rtl_newline_pitch(void);

uint16_t gate_rtl_palette_mode(void);
uint16_t gate_rtl_palette_video_type(void);
uint16_t gate_rtl_palette_status_port(void);
int16_t gate_rtl_palette_color_count(uint16_t offset);
uint16_t gate_rtl_palette_conversion(void);
uint8_t gate_rtl_palette_table_byte(uint16_t offset);

uint16_t gate_rtl_graphics_mode(void);
void gate_rtl_graphics_set_index(uint16_t value);
int16_t gate_rtl_graphics_byte_count(uint16_t index);
uint8_t gate_rtl_graphics_byte(uint16_t index);
void gate_rtl_graphics_set_byte(uint16_t index, uint8_t value);

#endif
