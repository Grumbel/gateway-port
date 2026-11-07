/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#ifndef GATE_LAUNCHER_INTRO_SCHEDULE_H
#define GATE_LAUNCHER_INTRO_SCHEDULE_H

/* Picture-resource arguments in the original GATE.EXE:1302..1a13 intro and
 * EXE:1a14..1c10 title interlude. IDs are original 16-bit operands, NOT
 * addresses. High byte selects the PIC file (0x31 -> GATE_301.PIC), low
 * byte selects its directory entry. Descriptions refer to the observed
 * caption/animation placement; they do not claim recovered source names.
 *
 * The order below follows the presentation, not resource-number order.
 * Some scenes are deliberately redrawn between successive captions. */
enum {
    GATE_INTRO_PICTURE_FIRST_SPLASH = 0x8101,             /* GATE_201.PIC #1 */
    GATE_INTRO_PICTURE_SECOND_SPLASH = 0x8100,            /* GATE_201.PIC #0 */
    GATE_INTRO_PICTURE_SHORT_PATH_END = 0x8102,          /* GATE_201.PIC #2 */

    GATE_INTRO_PICTURE_RED_BAR_TITLE = 0x3000,           /* GATE_300.PIC #0 */
    GATE_INTRO_PICTURE_RED_LOGO_FIRST_DELTA = 0x3001,    /* GATE_300.PIC #1 */
    GATE_INTRO_PICTURE_RED_LOGO_CREDIT = 0x301b,         /* GATE_300.PIC #27 */

    GATE_INTRO_PICTURE_CAPTIONS_01_02 = 0x3105,          /* GATE_301.PIC #5 */
    GATE_INTRO_PICTURE_OPENING_SHIP_FIRST = 0x3106,      /* GATE_301.PIC #6 */
    GATE_INTRO_PICTURE_CAPTIONS_03_04 = 0x3100,          /* GATE_301.PIC #0 */
    GATE_INTRO_PICTURE_CAPTION_05 = 0x3101,              /* GATE_301.PIC #1 */
    GATE_INTRO_PICTURE_CAPTIONS_06_07 = 0x3102,          /* GATE_301.PIC #2 */

    GATE_INTRO_PICTURE_CAPTIONS_17_18 = 0x332a,          /* GATE_303.PIC #42 */
    GATE_INTRO_PICTURE_STARFIELD_FIRST_DELTA = 0x332b,  /* GATE_303.PIC #43 */
    GATE_INTRO_PICTURE_CAPTIONS_08_11 = 0x3221,          /* GATE_302.PIC #33 */
    GATE_INTRO_PICTURE_STATION_LATER_DELTA = 0x323d,     /* GATE_302.PIC #61 */

    GATE_INTRO_PICTURE_CAPTION_12 = 0x3103,              /* GATE_301.PIC #3 */
    GATE_INTRO_PICTURE_CAPTIONS_13_14 = 0x3104,          /* GATE_301.PIC #4 */
    GATE_INTRO_PICTURE_CAPTION_15 = 0x3200,              /* GATE_302.PIC #0 */
    GATE_INTRO_PICTURE_APPROACH_FIRST_DELTA = 0x3201,   /* GATE_302.PIC #1 */
    GATE_INTRO_PICTURE_STATION_FIRST_DELTA = 0x3222,    /* GATE_302.PIC #34 */
    GATE_INTRO_PICTURE_EXIT_STARFIELD = 0x3300,         /* GATE_303.PIC #0 */
    GATE_INTRO_PICTURE_EXIT_SHIP_FIRST_DELTA = 0x3301,  /* GATE_303.PIC #1 */
    GATE_INTRO_PICTURE_CAPTION_16 = 0x3a04,              /* GATE_310.PIC #4 */
};

/* Original frame-count arguments at EXE:1302. The two preloaded sequences
 * both have 26 entries; the other five use separate (base, first, count)
 * operands. Count is not the resource number of the last frame. */
enum {
    GATE_INTRO_RED_LOGO_FRAMES = 26,
    GATE_INTRO_OPENING_SHIP_FRAMES = 26,
    GATE_INTRO_STARFIELD_PREVIEW_FRAMES = 37,           /* EXE:1743 */
    GATE_INTRO_STATION_LATER_FRAMES = 42,               /* EXE:1762 */
    GATE_INTRO_APPROACH_FRAMES = 32,                    /* EXE:191f */
    GATE_INTRO_STATION_FIRST_FRAMES = 27,               /* EXE:1933 */
    GATE_INTRO_EXIT_SHIP_FRAMES = 41,                   /* EXE:1947 */
};

/* Timing and caption geometry are source-code immediate operands, not
 * relocated memory addresses. Delays are original guest milliseconds; the
 * caption's per-character time is added with uint16_t wraparound. */
enum {
    GATE_INTRO_PROMPT_FONT = 1,
    GATE_INTRO_CAPTION_FONT = 5,
    GATE_INTRO_CAPTION_WORD_BUFFER_BYTES = 80,
    GATE_INTRO_CAPTION_TEXT_INSET = 5,
    GATE_INTRO_CAPTION_BOTTOM_INSET = 2,
    GATE_INTRO_CAPTION_BOTTOM_LIMIT = 185,
    GATE_INTRO_CAPTION_FRAME_STYLE = 4,
    GATE_INTRO_CAPTION_BASE_DURATION_MS = 2000,
    GATE_INTRO_CAPTION_PER_CHARACTER_MS = 45,
    GATE_INTRO_KEY_HINT_WAIT_MS = 3500,

    GATE_INTRO_RED_BAR_FIRST_DAC_COMPONENT = 45,
    GATE_INTRO_RED_BAR_COMPONENT_STRIDE = 3,
    GATE_INTRO_RED_BAR_MAX_DAC_LEVEL = 63,
    GATE_INTRO_RED_BAR_HOLD_MS = 500,
    GATE_INTRO_TITLE_PALETTE_PREFIX_BYTES = 45,
    GATE_INTRO_TITLE_MAX_DAC_LEVEL = 63,
    GATE_INTRO_TITLE_LEAD_IN_MS = 250,
    GATE_INTRO_TITLE_HOLD_MS = 2000,
    GATE_INTRO_TITLE_MIDPOINT_MS = 500,
    GATE_INTRO_PALETTE_SCALE_DENOMINATOR = 64,
    GATE_INTRO_PALETTE_FADE_STEP = 4,

    GATE_INTRO_SPLASH_WAIT_MS = 2000,
    GATE_INTRO_SHORT_PATH_WAIT_MS = 3000,
    GATE_INTRO_SCENE_SHORT_WAIT_MS = 1000,
    GATE_INTRO_SCENE_LONG_WAIT_MS = 2000,
    GATE_INTRO_RED_CREDIT_WAIT_MS = 3000,
};

#endif
