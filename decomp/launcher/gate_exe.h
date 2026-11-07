/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#ifndef GATE_LAUNCHER_EXE_DECOMP_H
#define GATE_LAUNCHER_EXE_DECOMP_H

#include <stdint.h>

/*
 * Manual C decompilation of GATE.EXE, not the original lost C source.
 * Function names describe behavior; see symbol-names.tsv and the function
 * comments for ORIGINAL FILE OFFSETS and observed linked FAR addresses.
 * Original source names and some full prototypes have not been recovered.
 *
 * These accessors are a test/host substitute for the original compiler's
 * DS/ES segmented pointers, not a proposed native engine interface.
 */
typedef struct GateFarPtr {
    uint16_t segment;
    uint16_t offset;
} GateFarPtr;

uint16_t gate_ds_word(uint16_t offset);
void gate_ds_set_word(uint16_t offset, uint16_t value);
uint16_t gate_ds_segment(void);
uint16_t gate_far_word(uint16_t segment, uint16_t offset);
uint8_t gate_far_byte(uint16_t segment, uint16_t offset);
void gate_far_set_byte(uint16_t segment, uint16_t offset, uint8_t value);
void gate_far_set_word(uint16_t segment, uint16_t offset, uint16_t value);

/* Linked 16-bit far calls; conventions inferred from caller instructions. */
uint16_t gate_linked_keyboard_pending(void); /* keyboard pending */
uint16_t gate_linked_read_keyboard_key(void); /* consume key */
uint16_t gate_linked_read_buffered_key(void); /* read one pending key, buffer except Space/CR */
uint16_t gate_linked_intro_mouse_flags(void); /* mouse flags in AL */
uint32_t gate_linked_exe_elapsed_milliseconds(void); /* clock returned in DX:AX */
uint16_t gate_linked_exe_string_length(GateFarPtr text); /* string length, AX */
void gate_linked_handle_control_key(void);
void gate_linked_select_intro_font(uint16_t value);
void gate_linked_set_intro_text_colors(uint16_t handle, uint16_t mode);
uint16_t gate_linked_measure_text_width(GateFarPtr string);
uint16_t gate_linked_font_character_advance(uint16_t character);
uint16_t gate_linked_font_line_advance(void);
void gate_linked_position_intro_text(int16_t x, int16_t y);
void gate_linked_draw_intro_text(GateFarPtr string);
void gate_linked_upload_intro_palette(GateFarPtr palette);
uint16_t gate_linked_allocate_intro_memory(uint16_t value);
void gate_linked_initialize_intro_services(void);
void gate_linked_clear_intro_palette(void);
void gate_linked_fade_intro_palette_out(void);
void gate_linked_fade_intro_palette_in(void);
void gate_linked_show_intro_picture(uint16_t image, uint16_t mode);
void gate_linked_play_intro_preloaded_sequence(uint16_t image, uint16_t value);
void gate_linked_play_intro_delta_sequence(uint16_t first, uint16_t next, uint16_t count);
void gate_linked_initialize_intro_picture_record(GateFarPtr buffer);
void gate_linked_load_intro_picture_record(GateFarPtr buffer, uint16_t image, uint16_t mode);
void gate_linked_draw_intro_picture_record(GateFarPtr buffer);
void gate_linked_release_intro_picture_record(GateFarPtr buffer);
void gate_linked_clear_intro_display(void);
void gate_linked_set_intro_video_mode(uint16_t value);
void gate_linked_set_intro_pen_color(uint16_t value);
void gate_linked_fill_intro_rectangle(uint16_t arg0, uint16_t arg1, uint16_t arg2,
                         uint16_t arg3, uint16_t arg4);
void gate_linked_reset_intro_mouse(void);
void gate_linked_draw_intro_caption_frame(uint16_t left, uint16_t top, uint16_t right,
                         uint16_t bottom, uint16_t style);
void gate_linked_flush_keyboard(void);

/* A stack-local buffer's SS:offset; a host substitute for 16-bit &local. */
GateFarPtr gate_stack_pointer(void *buffer);

/* GATE.EXE:0x0bb8; identity of the FAR pointer passed on the stack. */
GateFarPtr gate_exe_identity_text_pointer(GateFarPtr pointer);

/* Linked near calls not yet independently decompiled. */
void gate_exe_measure_caption(uint16_t *left, uint16_t *top, uint16_t *right,
                   uint16_t *bottom, GateFarPtr text);

/* The original routines use far return (`RETF`), with C-style stack args. */
uint16_t gate_exe_handle_intro_input(void);
void gate_exe_wait_interruptible(int16_t delay);
void gate_exe_reveal_red_bar(int16_t bar);
void gate_exe_reveal_credit_color(void);
void gate_exe_play_intro(void);
void gate_exe_prepare_intro_globals(void); /* extracted source block, not original entry */
void gate_exe_play_title_interlude(void);
void gate_exe_fade_in_picture(uint16_t image);
void gate_exe_show_caption(uint16_t left, uint16_t top, uint16_t right, GateFarPtr text);

#endif
