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
    GATE_RTL_SEQUENCE_LIMIT_MESSAGE_OFFSET = 0x14b2,
};

/* GATE.RTL resource selector's 12-byte on-disk picture-index entry.
 * These are byte displacements from GATE_RTL_HEADER_OFFSET (DS:005c in
 * the relocated header segment), not absolute resident DS offsets. */
enum {
    GATE_PICTURE_HEADER_FILE_OFFSET_LOW = 0x00,
    GATE_PICTURE_HEADER_FILE_OFFSET_HIGH = 0x02,
    GATE_PICTURE_HEADER_MODE_COUNT = 0x04,
    GATE_PICTURE_HEADER_FLAGS = 0x05,
    GATE_PICTURE_HEADER_WIDTH = 0x06,
    GATE_PICTURE_HEADER_HEIGHT = 0x08,
    GATE_PICTURE_HEADER_DELAY_BASE = 0x0a,
    GATE_PICTURE_HEADER_DELAY_SPREAD = 0x0b,
    GATE_PICTURE_HEADER_BYTES = 0x0c,
    GATE_PICTURE_HEADER_FORMAT_MASK = 0x0f,
    GATE_PICTURE_HEADER_HAS_PALETTE = 0x10,
    GATE_PICTURE_HEADER_SPECIAL_BLIT = 0x40,
};

/* Resource-ID bit layout in RTL:11ecb..120f1. The original WORD argument
 * is decoded into a three-digit GATE_###.PIC filename and a directory
 * entry number. Its high bit selects the live graphics-mode file group;
 * non-special IDs use a signed arithmetic right shift by 12. These are
 * ID masks/strides, not relocated memory offsets. */
enum {
    GATE_RESOURCE_USE_GRAPHICS_MODE_BIT = 0x8000,
    GATE_RESOURCE_GROUP_SHIFT = 12,
    GATE_RESOURCE_FILENAME_GROUP_MULTIPLIER = 100,
    GATE_RESOURCE_FILENAME_SUFFIX_MASK = 0x0f,
    GATE_RESOURCE_DIRECTORY_ENTRY_MASK = 0x00ff,
    GATE_RESOURCE_POSITION_PAIR_BYTES = 4,
    GATE_RESOURCE_NO_CACHED_FILE = 0xffff,
};

/* The RTL uses DS:1416+field to access a separate block of resident
 * resource-loader WORDs. Keep these displacements separate from the
 * absolute GATE_DS_RESOURCE_SCALARS_BASE and live segment selectors. */
enum {
    GATE_RESOURCE_ERROR_LATCH = 0x00,
    GATE_RESOURCE_DRAW_ORIGIN_X = 0x02,
    GATE_RESOURCE_DRAW_ORIGIN_Y = 0x04,
    GATE_RESOURCE_PALETTE_PENDING = 0x06,
    GATE_RESOURCE_CACHED_FILE_ID = 0x08,
    GATE_RESOURCE_PICTURE_X = 0x0a,
    GATE_RESOURCE_PICTURE_Y = 0x0c,
    GATE_RESOURCE_SEQUENCE_FILTER = 0x0e,
    GATE_RESOURCE_SKIP_PICTURE_PAYLOAD = 0x10,
};

/* Original 136-byte picture work record. Its VFX image starts at +08 and
 * has a distinct structure (the offsets below are relative to that image,
 * not to the beginning of the work record). */
enum {
    GATE_PICTURE_RECORD_BYTES = 0x88,
    GATE_PICTURE_RECORD_ACTIVE = 0x00,
    GATE_PICTURE_RECORD_BLIT_MODE = 0x01,
    GATE_PICTURE_RECORD_X = 0x02,
    GATE_PICTURE_RECORD_Y = 0x04,
    GATE_PICTURE_RECORD_DELAY_BASE = 0x06,
    GATE_PICTURE_RECORD_DELAY_SPREAD = 0x07,
    GATE_PICTURE_RECORD_VFX_IMAGE = 0x08,
    GATE_PICTURE_SEQUENCE_CAPACITY = 32,
    /* RTL:12d67 loads 256 records when the signed count is nonpositive;
     * this intentionally exceeds the preloaded player's 32-record array. */
    GATE_PICTURE_SEQUENCE_NONPOSITIVE_COUNT = 256,
    /* RTL:13115 and RTL:12f6b both scale picture header delay bytes by 50. */
    GATE_PICTURE_ANIMATION_DELAY_QUANTUM_MS = 50,
};

/* Picture-format tag and 16-bit image conversion operands in RTL:12131,
 * 121b5, 12205, 12383 and 12c33. The +/-1 tags are signed bytes from
 * the relocated table at DS:1480, not image-header flag bits. Names for
 * the coordinate transforms describe the observed arithmetic without
 * claiming an unverified on-disk format identity. */
enum {
    GATE_PICTURE_FORMAT_TAG_COMPACT_ROWS = 0xffff,
    GATE_PICTURE_FORMAT_TAG_EXPAND_ROWS = 1,
    GATE_PICTURE_COMPACT_PALETTE_BYTES = 16,
    GATE_PICTURE_EXPAND_PALETTE_BYTES = 48,
    GATE_PICTURE_WORD_SIGN_BIT = 0x8000,
    GATE_PICTURE_NEGATION_SIGN_MASK = 0xffff,
    GATE_PICTURE_QUARTER_SHIFT = 2,
    GATE_PICTURE_QUARTER_SAR_SIGN_FILL = 0xc000,
    GATE_PICTURE_QUARTER_DIVISOR = 4,
    GATE_PICTURE_THREE_QUARTERS_MULTIPLIER = 3,
    GATE_PICTURE_FOUR_THIRDS_SHIFT = 2,
    GATE_PICTURE_FOUR_THIRDS_DIVISOR = 3,
    GATE_PICTURE_DISCARDED_ROW_REMAINDER = 3,
    GATE_PICTURE_EXPANDED_LAST_ROW_REMAINDER = 2,
    GATE_PICTURE_VFX_ROW_STRIDE_FACTOR = 4,
    GATE_PICTURE_VFX_PLANE_ROWS_FACTOR = 4,
    GATE_PICTURE_PACKED_WIDTH_ROUNDUP = 7,
    GATE_PICTURE_PACKED_WIDTH_SHIFT = 3,
    GATE_PICTURE_PACKED_WIDTH_SAR_SIGN_FILL = 0xe000,
    GATE_PICTURE_SPECIAL_BLIT_MODE = 3,
    GATE_PICTURE_DEFAULT_BLIT_MODE = 0,
};

/* Resident VFX image header, beginning at work+08 (or at a FAR pointer
 * supplied directly by the 245d:0006 allocator). These are original
 * offsets observed in the 245d/24a7 services; reserved fields retain
 * neutral names until their purpose can be proven. */
enum {
    GATE_VFX_IMAGE_SIGNATURE = 0x00,
    GATE_VFX_IMAGE_KIND = 0x02,
    GATE_VFX_IMAGE_RESERVED_03 = 0x03,
    GATE_VFX_IMAGE_VIDEO_MODE = 0x04,
    GATE_VFX_IMAGE_LEFT = 0x06,
    GATE_VFX_IMAGE_TOP = 0x08,
    GATE_VFX_IMAGE_RIGHT = 0x0a,
    GATE_VFX_IMAGE_BOTTOM = 0x0c,
    GATE_VFX_IMAGE_MODE_WIDTH = 0x0e,
    GATE_VFX_IMAGE_MODE_HEIGHT = 0x10,
    GATE_VFX_IMAGE_PLANES = 0x12,
    GATE_VFX_IMAGE_BITS_PER_PLANE = 0x13,
    GATE_VFX_IMAGE_ROW_STRIDE = 0x14,
    GATE_VFX_IMAGE_FLAGS = 0x16,
    GATE_VFX_IMAGE_PIXELS_OFFSET = 0x18,
    GATE_VFX_IMAGE_PIXELS_SEGMENT = 0x1a,
    GATE_VFX_IMAGE_LINE_LAYOUT = 0x1c,
    GATE_VFX_IMAGE_RESERVED_1E = 0x1e,
    GATE_VFX_IMAGE_RESERVED_20 = 0x20,
    GATE_VFX_IMAGE_WIDTH = 0x2c,
    GATE_VFX_IMAGE_HEIGHT = 0x2e,
    GATE_VFX_IMAGE_ROW_BYTES = 0x30,
    GATE_VFX_IMAGE_PIXEL_MASK = 0x32,
    GATE_VFX_IMAGE_PLANE_ROWS = 0x34,
    GATE_VFX_IMAGE_HEADER_BYTES = 0x80,
    GATE_VFX_IMAGE_SIGNATURE_VALUE = 0xca00,
};

/* Resident 245d:00f7 / 2479:0002 / 245d:016c allocation conventions.
 * The 0x65 kind byte is verified, but its significance beyond the
 * constructor's descriptor field is not established. Mode indices here
 * describe the original resident VFX table, not BIOS INT 10h modes. */
enum {
    GATE_VFX_PLANAR_MODE = 2,
    GATE_VFX_CHUNKY_MODE = 3,
    GATE_VFX_PLANAR_WIDTH = 640,
    GATE_VFX_PLANAR_HEIGHT = 480,
    GATE_VFX_PLANAR_BITS_PER_PLANE = 1,
    GATE_VFX_PLANAR_PLANES = 4,
    GATE_VFX_CHUNKY_WIDTH = 320,
    GATE_VFX_CHUNKY_HEIGHT = 200,
    GATE_VFX_CHUNKY_BITS_PER_PLANE = 8,
    GATE_VFX_CHUNKY_PLANES = 1,
    GATE_VFX_ALLOCATOR_FAILURE = 0xffe6,
    GATE_VFX_ALLOCATOR_KIND_BYTE = 0x65,
    GATE_VFX_BITS_PER_BYTE = 8,
    GATE_VFX_LAST_BIT_INDEX = 7,
    GATE_VFX_ROW_ALIGNMENT_BYTES = 2,
    GATE_VFX_MAX_ALLOCATED_PIXEL_BYTES = 0xfffff,
    GATE_VFX_FAR_PIXEL_OFFSET_BITS = 16,
    GATE_VFX_FAR_PIXEL_SEGMENT_ADVANCE_BITS = 12,
};

/* Relocated FAR fields read during RTL:1266f before a picture is shown.
 * Numeric values are offsets within their respective linked FAR segments,
 * not positions inside the picture/VFX records. */
enum {
    GATE_PREPARE_STATUS_WORD = 0x70,
    GATE_PREPARE_SCRATCH_BYTES = 0x72,
    GATE_PREPARE_PEN_STATE_DEST = 0x7a,
    GATE_PREPARE_WORD_DEST = 0x7c,
    GATE_PREPARE_SCRATCH_COPY_DEST = 0x7e,
    GATE_PREPARE_FLAG_SOURCE = 0x82,
    GATE_PREPARE_WORD_SOURCE = 0x84,
    GATE_PREPARE_SCRATCH_COPY_SOURCE = 0x86,
    GATE_RTL_PREPARED_PALETTE_COPY_OFFSET = 0x56ce,
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
