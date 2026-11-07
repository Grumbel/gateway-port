/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#ifndef GATE_LAUNCHER_RTL_IMAGE_DECOMP_H
#define GATE_LAUNCHER_RTL_IMAGE_DECOMP_H

#include "gate_exe.h"

/* Original RTLink-relocated offsets, verified in the live GATE.RTL image.
 * The original RTL file contains zero placeholders here: these are not
 * offsets zero. The segment WORDs remain resolved at runtime through the
 * named external accessors. */
enum {
    GATE_RTL_VIDEO_MODE_OFFSET = 0x0fce,
    GATE_RTL_FILE_HANDLE_OFFSET = 0x005a,
    GATE_RTL_HEADER_OFFSET = 0x005c,
    GATE_RTL_SIZE_TABLE_OFFSET = 0x0fd2,
    GATE_RTL_TRANSFORM_OFFSET = 0x0fdc,
    GATE_RTL_FORMAT_TAGS_OFFSET = 0x1480,
    GATE_RTL_FORMAT_MODES_OFFSET = 0x1486,
    GATE_RTL_MESSAGE_OFFSET = 0x148b,
};

/* First original image-display entry, called at GATE.EXE 0f10:1014.
 * Return value is original AX=0 or 1, ignored by the launcher caller. */
uint16_t gate_rtl_show_picture(uint16_t resource, uint16_t mode);

/* GATE.RTL:0x12c33..0x12d66, original resource-loading subroutine. */
uint16_t gate_rtl_load_picture_record(GateFarPtr work, uint16_t resource, uint16_t mode);

/* GATE.RTL:0x11ecb..0x120f1, original resource selector and file reader. */
uint16_t gate_rtl_select_picture_resource(uint16_t resource, uint16_t mode);
/* GATE.RTL:0x12205..0x1230e post-load image preparation, with its
 * bounded original helper routines (all original file offsets). */
void gate_rtl_postprocess_picture(GateFarPtr work);
uint16_t gate_rtl_detect_picture_format(void);
void gate_rtl_prepare_picture_palette_translation(uint16_t format);
uint16_t gate_rtl_transform_picture_coordinate(uint16_t format, uint16_t value);
void gate_rtl_compact_picture_rows(GateFarPtr source, GateFarPtr destination,
                    int16_t first, int16_t last, uint16_t bytes_per_row);

/* RTL's relocated near-DS tables and display buffer, used by 120f3 and 12131. */
uint8_t gate_rtl_image_data_byte(uint16_t offset);
uint16_t gate_rtl_image_data_segment(void);
uint16_t gate_rtl_image_transform_segment(void); /* relocated DS:4ddc */
GateFarPtr gate_rtl_image_palette_source(void);  /* relocated source FAR ptr */

/* Three other original FAR calls with independently verified identities. */
GateFarPtr gate_linked_transform_picture_palette_indexed(GateFarPtr palette);
GateFarPtr gate_linked_transform_picture_palette_rgb(GateFarPtr palette);
void gate_linked_print_intro_diagnostic(GateFarPtr message);

/* Original dependencies of 0x12c33. All far service identities are
 * verified against a private, linked guest-RAM snapshot. */

/* RTL DS:4dce, DS:4dd0, DS:4dd8 hold the segments of relocated globals,
 * rather than flat host pointers. The accessor must preserve those WORDs. */
uint16_t gate_rtl_image_mode_segment(void);
uint16_t gate_rtl_image_palette_segment(void);
uint16_t gate_rtl_image_header_segment(void);

/* Bounded GATE.RTL:0x1266f..0x12771, image/video preparation. */
void gate_rtl_prepare_picture_display(void);
uint16_t gate_rtl_preparer_status_segment(void);  /* DS:4dd4 FAR pointer */
uint16_t gate_rtl_preparer_flag_source_segment(void); /* DS:4dd2 FAR pointer */
uint16_t gate_rtl_preparer_word_dest_segment(void); /* DS:4de2 FAR pointer */

/* Linked GATE.RTL:0x12c8c = 145d:00f7. The last two parameters of the
 * original call are a FAR output pointer and a zero-valued flag. */
uint16_t gate_linked_allocate_vfx_image(uint16_t header_word8, uint16_t header_word6,
                            uint16_t video_mode, GateFarPtr destination,
                            uint16_t zero_flag);
/* Linked 1479:0002 at RTL:0x12ca3, then 0eec:018a at RTL:0x12d33. */
void gate_linked_initialize_vfx_image(uint16_t zero_flag, GateFarPtr destination);
uint16_t gate_linked_decompress_picture_pixels(uint16_t work_word20, uint16_t work_word22);

/* RTL's original near DS:0000..0010 scalar words are relocated by +1416;
 * segment words at DS:4dd6 (file handle), DS:4dda (format lengths). */
uint16_t gate_rtl_resource_ds_word(uint16_t offset);
void gate_rtl_resource_ds_set_word(uint16_t offset, uint16_t value);
uint16_t gate_rtl_resource_handle_segment(void);
uint16_t gate_rtl_resource_size_table_segment(void);

/* Original far services reached by RTL:0x11ecb, linked into 0c33.
 * The reader receives the source's relocated FAR output pointer. */
void gate_linked_close_resource_file(uint16_t handle);
uint16_t gate_linked_open_resource_file(uint16_t zero_word, uint16_t resource_id);
void gate_linked_seek_resource_file(uint16_t handle, uint32_t offset);
uint16_t gate_linked_read_resource_bytes(uint16_t handle, GateFarPtr destination,
                            uint16_t word_arg);
uint16_t gate_linked_read_resource_word(uint16_t handle);

/* Original image draw wrapper and its 136-byte-record array helpers. */
void gate_rtl_draw_picture_record(GateFarPtr work);
void gate_rtl_draw_picture_record_sequence(GateFarPtr work, int16_t count);
void gate_rtl_play_timed_picture_records(GateFarPtr work, int16_t count);
void gate_rtl_initialize_picture_record_array(GateFarPtr work, int16_t count);
void gate_rtl_release_picture_record_array(GateFarPtr work, int16_t count);

/* Original consecutive-resource loader and 32-record intro animation entry. */
uint16_t gate_rtl_load_picture_sequence(GateFarPtr work, int16_t count,
                        uint16_t first_resource, uint16_t mode);
void gate_rtl_play_preloaded_picture_sequence(uint16_t first_resource, int16_t count);
/* Uses the existing gate_linked_print_intro_diagnostic print service at RTL DS:14b2. */

/* Verified original linked FAR identities used by the image processor.
 * The 24a7 service consumes its 12-byte argument block in the callee. */
uint16_t gate_linked_save_vfx_draw_state(void);
void gate_linked_blit_vfx_picture(uint16_t zero, uint16_t y, uint16_t x,
                        uint16_t signed_mode, GateFarPtr pixels);
void gate_linked_restore_vfx_draw_state(uint16_t saved);
void gate_linked_seed_runtime_random(uint16_t seed); /* original C runtime srand */
uint16_t gate_linked_runtime_random(void);
void gate_linked_wait_animation_delay(uint32_t delay);

/* Original intro animation controller: GATE.RTL:0x12f6b..0x130aa,
 * linked as GATE.EXE:0f10:1466 (all three original stack WORDs). */
void gate_rtl_play_picture_delta_sequence(uint16_t first, uint16_t next, int16_t count);
uint16_t gate_rtl_anim_segment_22(void); /* linked DS:4de6 segment */
uint16_t gate_rtl_anim_segment_24(void); /* linked DS:4de8 segment */
uint16_t gate_rtl_anim_segment_26(void); /* linked DS:4dea segment */
void gate_linked_set_intro_pen_color(uint16_t video_value);
void gate_linked_fill_intro_rectangle(uint16_t two, uint16_t zero_a, uint16_t zero_b,
                         uint16_t video_value_b, uint16_t video_value_a);
uint32_t gate_linked_animation_elapsed_milliseconds(void);
uint16_t gate_linked_poll_animation_input(void);

#endif
