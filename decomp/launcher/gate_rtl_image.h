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
uint16_t gate_rtl_12b19(uint16_t resource, uint16_t mode);

/* GATE.RTL:0x12c33..0x12d66, original resource-loading subroutine. */
uint16_t gate_rtl_12c33(GateFarPtr work, uint16_t resource, uint16_t mode);

/* GATE.RTL:0x11ecb..0x120f1, original resource selector and file reader. */
uint16_t gate_rtl_11ecb(uint16_t resource, uint16_t mode);
/* GATE.RTL:0x12205..0x1230e post-load image preparation, with its
 * bounded original helper routines (all original file offsets). */
void gate_rtl_12205(GateFarPtr work);
uint16_t gate_rtl_120f3(void);
void gate_rtl_12131(uint16_t format);
uint16_t gate_rtl_121b5(uint16_t format, uint16_t value);
void gate_rtl_12383(GateFarPtr source, GateFarPtr destination,
                    int16_t first, int16_t last, uint16_t bytes_per_row);

/* RTL's relocated near-DS tables and display buffer, used by 120f3 and 12131. */
uint8_t gate_rtl_image_data_byte(uint16_t offset);
uint16_t gate_rtl_image_data_segment(void);
uint16_t gate_rtl_image_transform_segment(void); /* relocated DS:4ddc */
GateFarPtr gate_rtl_image_palette_source(void);  /* relocated source FAR ptr */

/* Three other original FAR calls with independently verified identities. */
GateFarPtr gate_far_0cdc_0430(GateFarPtr palette);
GateFarPtr gate_far_0cdc_0396(GateFarPtr palette);
void gate_far_1258_172e(GateFarPtr message);

/* Original dependencies of 0x12c33. All far service identities are
 * verified against a private, linked guest-RAM snapshot. */

/* RTL DS:4dce, DS:4dd0, DS:4dd8 hold the segments of relocated globals,
 * rather than flat host pointers. The accessor must preserve those WORDs. */
uint16_t gate_rtl_image_mode_segment(void);
uint16_t gate_rtl_image_palette_segment(void);
uint16_t gate_rtl_image_header_segment(void);

/* Bounded GATE.RTL:0x1266f..0x12771, image/video preparation. */
void gate_rtl_1266f(void);
uint16_t gate_rtl_preparer_status_segment(void);  /* DS:4dd4 FAR pointer */
uint16_t gate_rtl_preparer_flag_source_segment(void); /* DS:4dd2 FAR pointer */
uint16_t gate_rtl_preparer_word_dest_segment(void); /* DS:4de2 FAR pointer */

/* Linked GATE.RTL:0x12c8c = 145d:00f7. The last two parameters of the
 * original call are a FAR output pointer and a zero-valued flag. */
uint16_t gate_far_145d_00f7(uint16_t header_word8, uint16_t header_word6,
                            uint16_t video_mode, GateFarPtr destination,
                            uint16_t zero_flag);
/* Linked 1479:0002 at RTL:0x12ca3, then 0eec:018a at RTL:0x12d33. */
void gate_far_1479_0002(uint16_t zero_flag, GateFarPtr destination);
uint16_t gate_far_0eec_018a(uint16_t work_word20, uint16_t work_word22);

/* RTL's original near DS:0000..0010 scalar words are relocated by +1416;
 * segment words at DS:4dd6 (file handle), DS:4dda (format lengths). */
uint16_t gate_rtl_resource_ds_word(uint16_t offset);
void gate_rtl_resource_ds_set_word(uint16_t offset, uint16_t value);
uint16_t gate_rtl_resource_handle_segment(void);
uint16_t gate_rtl_resource_size_table_segment(void);

/* Original far services reached by RTL:0x11ecb, linked into 0c33.
 * The reader receives the source's relocated FAR output pointer. */
void gate_far_0c33_019a(uint16_t handle);
uint16_t gate_far_0c33_0040(uint16_t zero_word, uint16_t resource_id);
void gate_far_0c33_0091(uint16_t handle, uint32_t offset);
uint16_t gate_far_0c33_00f2(uint16_t handle, GateFarPtr destination,
                            uint16_t word_arg);
uint16_t gate_far_0c33_00d4(uint16_t handle);

/* Original image draw wrapper and its 136-byte-record array helpers. */
void gate_rtl_12ed7(GateFarPtr work);
void gate_rtl_12f2b(GateFarPtr work, int16_t count);
void gate_rtl_13115(GateFarPtr work, int16_t count);
void gate_rtl_13191(GateFarPtr work, int16_t count);
void gate_rtl_131f7(GateFarPtr work, int16_t count);

/* Verified original linked FAR identities used by the image processor.
 * The 24a7 service consumes its 12-byte argument block in the callee. */
uint16_t gate_far_1e48_04d9(void);
void gate_far_24a7_000c(uint16_t zero, uint16_t y, uint16_t x,
                        uint16_t signed_mode, GateFarPtr pixels);
void gate_far_1e48_03c5(uint16_t saved);
uint16_t gate_far_12a5_3b0c(void);
void gate_far_223f_00b6(uint32_t delay);

/* Original intro animation controller: GATE.RTL:0x12f6b..0x130aa,
 * linked as GATE.EXE:0f10:1466 (all three original stack WORDs). */
void gate_rtl_12f6b(uint16_t first, uint16_t next, int16_t count);
uint16_t gate_rtl_anim_segment_22(void); /* linked DS:4de6 segment */
uint16_t gate_rtl_anim_segment_24(void); /* linked DS:4de8 segment */
uint16_t gate_rtl_anim_segment_26(void); /* linked DS:4dea segment */
void gate_far_0cdc_0aac(uint16_t video_value);
void gate_far_0cdc_0aee(uint16_t two, uint16_t zero_a, uint16_t zero_b,
                         uint16_t video_value_b, uint16_t video_value_a);
uint32_t gate_far_12a5_38c4(void);
uint16_t gate_far_1000_0008(void);

#endif
