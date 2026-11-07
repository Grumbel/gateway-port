/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_exe.h"
#include "gate_intro_schedule.h"
#include "gate_rtl_image.h"
#include "gate_rtl_mode.h"

static GateFarPtr far_pointer(uint16_t segment, uint16_t offset)
{
    GateFarPtr result = {segment, offset};
    return result;
}

/* The on-disk EXE encodes segment 1a7a in eleven palette-upload operands.
 * The DOS-linked code actually executes with segment 2a7a in those same
 * operands. Resolve this loader relocation through the linked palette-bank
 * selector instead of reading the unrelocated code bank at 1b77c. */
static GateFarPtr runtime_palette_pointer(void)
{
    return far_pointer(gate_ds_word(GATE_DS_INTRO_PALETTE_SEGMENT), GATE_INTRO_PALETTE_BANK_OFFSET);
}

/* GATE.EXE file 0x0e37..0x0f17. Key handling and intro interruption.
 *
 * The RTL calls are still named by their original linked segment:offset.
 * Stack-check far call 02a5:0c08 is compiler machinery, not source logic.
 * The control-key branch is intentionally not treated as ordinary Escape.
 */
uint16_t gate_exe_handle_intro_input(void)
{
    uint16_t key;
    uint16_t width;
    uint16_t height;

    if (gate_linked_keyboard_pending() == 0) {
        if ((gate_linked_intro_mouse_flags() & GATE_INTRO_INPUT_MOUSE_SKIP_MASK) != 0)
            return 1;
        return 0;
    }

    key = gate_linked_read_keyboard_key();
    if (key == GATE_INTRO_INPUT_CONTROL_KEY) {
        gate_linked_handle_control_key();
        return 0;
    }
    if (key == GATE_INTRO_INPUT_ESCAPE)
        return 1;

    if (gate_ds_word(GATE_DS_INTRO_KEY_SEEN) != 0)
        return 0;
    gate_ds_set_word(GATE_DS_INTRO_KEY_SEEN, 1);
    gate_linked_select_intro_font(GATE_INTRO_PROMPT_FONT);
    gate_linked_set_intro_text_colors(gate_far_word(gate_ds_word(GATE_DS_INTRO_PROMPT_COLOR_SEGMENT), GATE_MODE_DESC_MAX_PEN_COLOR), GATE_INTRO_TEXT_TRANSPARENT_BACKGROUND);
    width = gate_linked_measure_text_width(far_pointer(gate_ds_segment(), GATE_DS_INTRO_KEY_HINT_TEXT));
    height = gate_linked_font_line_advance();
    gate_linked_position_intro_text(
        (int16_t)((int16_t)(gate_far_word(gate_ds_word(GATE_DS_INTRO_SCREEN_WIDTH_SEGMENT), GATE_MODE_DESC_SCREEN_WIDTH) - width) / 2),
        (int16_t)(gate_far_word(gate_ds_word(GATE_DS_INTRO_SCREEN_HEIGHT_SEGMENT), GATE_MODE_DESC_SCREEN_HEIGHT) - height - 1));
    gate_linked_draw_intro_text(far_pointer(gate_ds_segment(), GATE_DS_INTRO_KEY_HINT_TEXT));
    if (gate_far_word(gate_ds_word(GATE_DS_INTRO_MODE_SEGMENT), GATE_DS_GRAPHICS_MODE) != GATE_RTL_VIDEO_MODE_VGA_256) {
        gate_exe_wait_interruptible(GATE_INTRO_KEY_HINT_WAIT_MS);
        gate_linked_read_buffered_key();
    }
    return 0;
}

/* GATE.EXE file 0x0f18..0x0f60. Interruptible clock delay.
 *
 * Original assembly compares HIGH words as signed 16-bit numbers and LOW
 * words as unsigned, not as a plain unsigned 32-bit timestamp comparison.
 * The delay is sign-extended by CWD and then added with ADC carry.
 */
void gate_exe_wait_interruptible(int16_t delay)
{
    uint32_t deadline;
    uint32_t now;

    deadline = gate_linked_exe_elapsed_milliseconds() + (uint32_t)(int32_t)delay;
    for (;;) {
        now = gate_linked_exe_elapsed_milliseconds();
        if ((int16_t)(now >> 16) > (int16_t)(deadline >> 16) ||
            ((int16_t)(now >> 16) == (int16_t)(deadline >> 16) &&
             (uint16_t)now >= (uint16_t)deadline))
            break;
        if (gate_linked_keyboard_pending() != 0)
            break;
        if (gate_linked_intro_mouse_flags() != 0)
            break;
    }
}

/* GATE.EXE file 0x0f61..0x1039. The original has a 0x38c-byte stack
 * frame: 0x8c bytes of picture work area, 0x300 bytes of temporary DAC
 * palette, and two 16-bit loop counters. This is a palette fade wrapped
 * around a set of still-unresolved image operations, not a timer table.
 *
 * CBW makes each original palette component a SIGNED byte, IMUL multiplies
 * by the factor, and the CWD/XOR/SUB/SAR sequence implements signed
 * division by 64 rounded toward zero. Given factors 0..60 and one-byte
 * components, the signed product stays within 16 bits.
 */
void gate_exe_fade_in_picture(uint16_t image)
{
    uint8_t work[GATE_EXE_FADE_PICTURE_WORK_BYTES];
    uint8_t faded[GATE_INTRO_PALETTE_RGB_BYTES];
    int16_t factor;
    int16_t i;

    gate_linked_fade_intro_palette_out();
    gate_linked_initialize_intro_picture_record(gate_stack_pointer(work));
    gate_linked_load_intro_picture_record(gate_stack_pointer(work), image, GATE_PICTURE_POSITION_ORIGIN);

    for (i = 0; i < GATE_INTRO_PALETTE_RGB_BYTES; ++i)
        faded[i] = 0;
    gate_linked_upload_intro_palette(gate_stack_pointer(faded));
    gate_linked_draw_intro_picture_record(gate_stack_pointer(work));
    gate_linked_release_intro_picture_record(gate_stack_pointer(work));

    for (factor = 0; factor < GATE_INTRO_PALETTE_SCALE_DENOMINATOR;
         factor += GATE_INTRO_PALETTE_FADE_STEP) {
        for (i = 0; i < GATE_INTRO_PALETTE_RGB_BYTES; ++i) {
            int16_t product = (int16_t)((int8_t)gate_far_byte(
                gate_ds_word(GATE_DS_INTRO_PALETTE_SEGMENT), (uint16_t)(GATE_INTRO_PALETTE_BANK_OFFSET + i)) * factor);
            faded[i] = (uint8_t)(product / GATE_INTRO_PALETTE_SCALE_DENOMINATOR);
        }
        gate_linked_upload_intro_palette(gate_stack_pointer(faded));
    }
    gate_linked_upload_intro_palette(runtime_palette_pointer());
}

/* GATE.EXE file 0x103a..0x117d. Measure an intro text box before drawing.
 *
 * The original function receives FAR POINTERS to the caller's left, top,
 * right and output-bottom words. They are passed as &stack locals, not
 * copied values. The four pointers mutate the caller's parameters in
 * place, including a signed y-position adjustment when bottom > 185.
 *
 * There is one 80-byte local word buffer at BP-5a, terminated at every
 * word. The original does NOT test its capacity (ORIGINAL BUG: a word of
 * 80 or more bytes overwrites nearby stack locals). We do not truncate it.
 * String traversal increments only the 16-bit offset of the far pointer.
 */
void gate_exe_measure_caption(uint16_t *left, uint16_t *top, uint16_t *right,
                   uint16_t *bottom, GateFarPtr text)
{
    char word[GATE_INTRO_CAPTION_WORD_BUFFER_BYTES];
    uint16_t cursor = text.offset;
    uint16_t x;
    uint16_t y;

    gate_linked_select_intro_font(GATE_INTRO_CAPTION_FONT);
    *left = (uint16_t)(*left + GATE_INTRO_CAPTION_TEXT_INSET);
    *top = (uint16_t)(*top + GATE_INTRO_CAPTION_TEXT_INSET);
    *right = (uint16_t)(*right - GATE_INTRO_CAPTION_TEXT_INSET);
    x = *left;
    y = *top;

    while (gate_far_byte(text.segment, cursor) != 0) {
        uint16_t count = 0;
        uint16_t width;
        uint8_t separator;

        while ((separator = gate_far_byte(text.segment, cursor)) != 0 &&
               separator != ' ' && separator != '\n') {
            word[count++] = (char)separator;
            cursor = (uint16_t)(cursor + 1);
        }
        word[count] = '\0';
        width = gate_linked_measure_text_width(gate_stack_pointer(word));

        if ((int16_t)(uint16_t)(x + width) > (int16_t)*right) {
            x = *left;
            y = (uint16_t)(y + gate_linked_font_line_advance());
        }
        x = (uint16_t)(x + width);

        separator = gate_far_byte(text.segment, cursor);
        if (separator == '\n') {
            x = *left;
            y = (uint16_t)(y + gate_linked_font_line_advance());
            cursor = (uint16_t)(cursor + 1);
        } else if (separator == ' ') {
            x = (uint16_t)(x + gate_linked_font_character_advance(' '));
            while (gate_far_byte(text.segment, cursor) == ' ')
                cursor = (uint16_t)(cursor + 1);
        }
    }

    *bottom = (uint16_t)(gate_linked_font_line_advance() + y - GATE_INTRO_CAPTION_BOTTOM_INSET);
    if ((int16_t)*bottom > GATE_INTRO_CAPTION_BOTTOM_LIMIT) {
        *top = (uint16_t)(*top - (uint16_t)(*bottom - GATE_INTRO_CAPTION_BOTTOM_LIMIT));
        *bottom = GATE_INTRO_CAPTION_BOTTOM_LIMIT;
    }
}

/* GATE.EXE file 0x117e..0x1301. The caption-display routine.
 *
 * Exactly two passes over the original text: the near 103a call measures
 * and MOVES the local arguments left/top/right, then 117e draws the frame
 * and prints each word using the same delimiter and wrap logic. The word
 * buffer is again 80 bytes and has the same unchecked original overflow.
 * The final wait is uint16_t(2000 + 45 * strlen(text)), not a table lookup.
 */
void gate_exe_show_caption(uint16_t left, uint16_t top, uint16_t right, GateFarPtr text)
{
    char word[GATE_INTRO_CAPTION_WORD_BUFFER_BYTES];
    uint16_t bottom;
    uint16_t cursor;
    uint16_t x;
    uint16_t y;

    gate_linked_select_intro_font(GATE_INTRO_CAPTION_FONT);
    gate_linked_set_intro_text_colors(gate_far_word(gate_ds_word(GATE_DS_INTRO_CAPTION_COLOR_SEGMENT), GATE_MODE_DESC_FRAME_COLOR_PRIMARY), GATE_INTRO_TEXT_TRANSPARENT_BACKGROUND);
    gate_exe_measure_caption(&left, &top, &right, &bottom, text);
    gate_linked_draw_intro_caption_frame((uint16_t)(left - GATE_INTRO_CAPTION_TEXT_INSET),
                         (uint16_t)(top - GATE_INTRO_CAPTION_TEXT_INSET),
                         (uint16_t)(right + GATE_INTRO_CAPTION_TEXT_INSET),
                         (uint16_t)(bottom + GATE_INTRO_CAPTION_TEXT_INSET),
                         GATE_INTRO_CAPTION_FRAME_STYLE);

    x = left;
    y = top;
    cursor = text.offset;
    while (gate_far_byte(text.segment, cursor) != 0) {
        uint16_t count = 0;
        uint16_t width;
        uint8_t separator;

        while ((separator = gate_far_byte(text.segment, cursor)) != 0 &&
               separator != ' ' && separator != '\n') {
            word[count++] = (char)separator;
            cursor = (uint16_t)(cursor + 1);
        }
        word[count] = '\0';
        width = gate_linked_measure_text_width(gate_stack_pointer(word));

        if ((int16_t)(uint16_t)(x + width) > (int16_t)right) {
            x = left;
            y = (uint16_t)(y + gate_linked_font_line_advance());
        }
        gate_linked_position_intro_text((int16_t)x, (int16_t)y);
        gate_linked_draw_intro_text(gate_stack_pointer(word));
        x = (uint16_t)(x + width);

        separator = gate_far_byte(text.segment, cursor);
        if (separator == '\n') {
            x = left;
            y = (uint16_t)(y + gate_linked_font_line_advance());
            cursor = (uint16_t)(cursor + 1);
        } else if (separator == ' ') {
            x = (uint16_t)(x + gate_linked_font_character_advance(' '));
            while (gate_far_byte(text.segment, cursor) == ' ')
                cursor = (uint16_t)(cursor + 1);
        }
    }
    gate_exe_wait_interruptible((int16_t)(uint16_t)(GATE_INTRO_CAPTION_BASE_DURATION_MS +
        (uint16_t)(GATE_INTRO_CAPTION_PER_CHARACTER_MS * gate_linked_exe_string_length(text))));
}

/* GATE.EXE file 0x1c11..0x1c6b. One of the seven red DAC bars.
 *
 * `bar * 3 + 45` is computed by signed IMUL, low word only; the loop byte
 * starts at zero and runs through 63 inclusive. If a key is pending, the
 * ENTIRE fade and its final 500ms wait are skipped.
 */
void gate_exe_reveal_red_bar(int16_t bar)
{
    uint16_t index;
    uint8_t value;

    if (gate_linked_keyboard_pending() != 0)
        return;

    index = (uint16_t)(GATE_INTRO_RED_BAR_FIRST_DAC_COMPONENT +
                       GATE_INTRO_RED_BAR_COMPONENT_STRIDE * (int32_t)bar);
    for (value = 0; value <= GATE_INTRO_RED_BAR_MAX_DAC_LEVEL; ++value) {
        gate_far_set_byte(gate_ds_word(GATE_DS_INTRO_PALETTE_SEGMENT), (uint16_t)(GATE_INTRO_PALETTE_BANK_OFFSET + index), value);
        gate_linked_upload_intro_palette(runtime_palette_pointer());
    }
    gate_exe_wait_interruptible(GATE_INTRO_RED_BAR_HOLD_MS);
}

/* GATE.EXE file 0x1c6c..0x1caa. Grayscale title credit reveal.
 * Unlike 0x1c11, there is no keyboard test and no final delay.
 */
void gate_exe_reveal_credit_color(void)
{
    uint8_t value;
    uint16_t segment;

    for (value = 0; value <= GATE_INTRO_TITLE_MAX_DAC_LEVEL; ++value) {
        segment = gate_ds_word(GATE_DS_INTRO_PALETTE_SEGMENT);
        gate_far_set_byte(segment, GATE_INTRO_PALETTE_CREDIT_RGB, value);
        gate_far_set_byte(segment, (GATE_INTRO_PALETTE_CREDIT_RGB + 1), value);
        gate_far_set_byte(segment, (GATE_INTRO_PALETTE_CREDIT_RGB + 2), value);
        gate_linked_upload_intro_palette(runtime_palette_pointer());
    }
}

/* GATE.EXE file 0x1302..0x1a13. This is the WHOLE enclosing launcher
 * presentation function: 0x13fc is a branch target, not a function entry.
 *
 * Call argument ordering below follows the original 16-bit C stack, with
 * the leftmost argument closest to the return address. The five-argument
 * 0cdc:0aee service remains deliberately unnamed: its meaning is unknown.
 * No frame-buffer schedule is used to reconstruct this control flow.
 *
 * The `goto cleanup` statements reproduce jumps to 0x1a09. The short path
 * skips that cleanup and joins only at 0x1a0e. The signed comparison at
 * 0x1392 is likewise preserved (JGE, not unsigned JAE).
 */
void gate_exe_play_intro(void)
{
    uint16_t mode;

    gate_exe_prepare_intro_globals();
    gate_linked_initialize_intro_services();

    mode = gate_far_word(gate_ds_word(GATE_DS_INTRO_MODE_SEGMENT), GATE_DS_GRAPHICS_MODE);
    if (mode == 0) {
        gate_linked_show_intro_picture(GATE_INTRO_PICTURE_FIRST_SPLASH, GATE_PICTURE_POSITION_FIRST_PAIR);
        gate_linked_show_intro_picture(GATE_INTRO_PICTURE_SECOND_SPLASH, GATE_PICTURE_POSITION_FIRST_PAIR);
        gate_exe_wait_interruptible(GATE_INTRO_SPLASH_WAIT_MS);
    } else {
        gate_exe_play_title_interlude();
    }

    mode = gate_far_word(gate_ds_word(GATE_DS_INTRO_MODE_SEGMENT), GATE_DS_GRAPHICS_MODE);
    if ((int16_t)mode < GATE_INTRO_RGB_PALETTE_MODE_START) {
        gate_linked_clear_intro_palette();
        gate_linked_clear_intro_display();
        if (gate_far_word(gate_ds_word(GATE_DS_INTRO_MODE_SEGMENT), GATE_DS_GRAPHICS_MODE) == GATE_INTRO_SHORT_PATH_PAINT_MODE) {
            gate_linked_set_intro_pen_color(GATE_INTRO_SHORT_PATH_PEN_COLOR);
            gate_linked_fill_intro_rectangle(
                GATE_INTRO_FULL_SCREEN_FILL_STYLE, 0, 0,
                gate_far_word(gate_ds_word(GATE_DS_INTRO_SCREEN_WIDTH_SEGMENT), GATE_MODE_DESC_SCREEN_WIDTH),
                gate_far_word(gate_ds_word(GATE_DS_INTRO_SCREEN_HEIGHT_SEGMENT), GATE_MODE_DESC_SCREEN_HEIGHT));
        }
        gate_linked_show_intro_picture(GATE_INTRO_PICTURE_SHORT_PATH_END, GATE_PICTURE_POSITION_FIRST_PAIR);
        gate_exe_wait_interruptible(GATE_INTRO_SHORT_PATH_WAIT_MS);
        goto finish;
    }

    if (gate_exe_handle_intro_input() != 0)
        goto cleanup;
    gate_linked_fade_intro_palette_out();
    gate_linked_set_intro_video_mode(GATE_RTL_VIDEO_MODE_VGA_256);
    gate_linked_reset_intro_mouse();
    gate_linked_clear_intro_display();
    gate_linked_show_intro_picture(GATE_INTRO_PICTURE_RED_BAR_TITLE, GATE_PICTURE_POSITION_FIRST_PAIR);
    if (gate_ds_word(GATE_DS_INTRO_KEY_SEEN) == 0 && gate_exe_handle_intro_input() != 0)
        goto cleanup;
    gate_exe_wait_interruptible(GATE_INTRO_SCENE_SHORT_WAIT_MS);
    if (gate_ds_word(GATE_DS_INTRO_KEY_SEEN) == 0 && gate_exe_handle_intro_input() != 0)
        goto cleanup;

    gate_exe_reveal_red_bar(1);
    if (gate_ds_word(GATE_DS_INTRO_KEY_SEEN) == 0 && gate_exe_handle_intro_input() != 0)
        goto cleanup;
    gate_exe_reveal_red_bar(6);
    if (gate_ds_word(GATE_DS_INTRO_KEY_SEEN) == 0 && gate_exe_handle_intro_input() != 0)
        goto cleanup;
    gate_exe_reveal_red_bar(4);
    if (gate_ds_word(GATE_DS_INTRO_KEY_SEEN) == 0 && gate_exe_handle_intro_input() != 0)
        goto cleanup;
    gate_exe_reveal_red_bar(7);
    if (gate_ds_word(GATE_DS_INTRO_KEY_SEEN) == 0 && gate_exe_handle_intro_input() != 0)
        goto cleanup;
    gate_exe_reveal_red_bar(2);
    if (gate_ds_word(GATE_DS_INTRO_KEY_SEEN) == 0 && gate_exe_handle_intro_input() != 0)
        goto cleanup;
    gate_exe_reveal_red_bar(5);
    if (gate_ds_word(GATE_DS_INTRO_KEY_SEEN) == 0 && gate_exe_handle_intro_input() != 0)
        goto cleanup;
    gate_exe_reveal_red_bar(3);
    if (gate_ds_word(GATE_DS_INTRO_KEY_SEEN) == 0 && gate_exe_handle_intro_input() != 0)
        goto cleanup;

    if (gate_linked_keyboard_pending() == 0)
        gate_linked_play_intro_preloaded_sequence(GATE_INTRO_PICTURE_RED_LOGO_FIRST_DELTA, GATE_INTRO_RED_LOGO_FRAMES);
    if (gate_linked_keyboard_pending() == 0) {
        gate_linked_show_intro_picture(GATE_INTRO_PICTURE_RED_LOGO_CREDIT, GATE_PICTURE_POSITION_FIRST_PAIR);
        gate_exe_wait_interruptible(GATE_INTRO_SCENE_SHORT_WAIT_MS);
    }
    if (gate_linked_keyboard_pending() == 0) {
        gate_exe_reveal_credit_color();
        gate_exe_wait_interruptible(GATE_INTRO_RED_CREDIT_WAIT_MS);
    }

    gate_ds_set_word(GATE_DS_INTRO_KEY_SEEN, 1);
    if (gate_exe_handle_intro_input() != 0)
        goto cleanup;
    gate_exe_fade_in_picture(GATE_INTRO_PICTURE_CAPTIONS_01_02);
    gate_exe_wait_interruptible(GATE_INTRO_SCENE_SHORT_WAIT_MS);
    if (gate_exe_handle_intro_input() != 0)
        goto cleanup;

    gate_exe_show_caption(10, 120, 310, far_pointer(gate_ds_segment(), GATE_DS_INTRO_CAPTION_01));
    if (gate_exe_handle_intro_input() != 0)
        goto cleanup;
    gate_linked_show_intro_picture(GATE_INTRO_PICTURE_CAPTIONS_01_02, GATE_PICTURE_POSITION_ORIGIN);
    gate_linked_play_intro_preloaded_sequence(GATE_INTRO_PICTURE_OPENING_SHIP_FIRST, GATE_INTRO_OPENING_SHIP_FRAMES);
    gate_exe_wait_interruptible(GATE_INTRO_SCENE_SHORT_WAIT_MS);
    if (gate_exe_handle_intro_input() != 0)
        goto cleanup;

    gate_exe_show_caption(10, 100, 180, far_pointer(gate_ds_segment(), GATE_DS_INTRO_CAPTION_02));
    if (gate_exe_handle_intro_input() != 0)
        goto cleanup;
    gate_exe_fade_in_picture(GATE_INTRO_PICTURE_CAPTIONS_03_04);
    gate_exe_wait_interruptible(GATE_INTRO_SCENE_SHORT_WAIT_MS);
    gate_exe_show_caption(10, 10, 160, far_pointer(gate_ds_segment(), GATE_DS_INTRO_CAPTION_03));
    if (gate_exe_handle_intro_input() != 0)
        goto cleanup;
    gate_linked_show_intro_picture(GATE_INTRO_PICTURE_CAPTIONS_03_04, GATE_PICTURE_POSITION_ORIGIN);
    gate_exe_show_caption(10, 10, 310, far_pointer(gate_ds_segment(), GATE_DS_INTRO_CAPTION_04));
    if (gate_exe_handle_intro_input() != 0)
        goto cleanup;

    gate_exe_fade_in_picture(GATE_INTRO_PICTURE_CAPTION_05);
    gate_exe_wait_interruptible(GATE_INTRO_SCENE_LONG_WAIT_MS);
    gate_exe_show_caption(160, 10, 310, far_pointer(gate_ds_segment(), GATE_DS_INTRO_CAPTION_05));
    if (gate_exe_handle_intro_input() != 0)
        goto cleanup;
    gate_exe_fade_in_picture(GATE_INTRO_PICTURE_CAPTIONS_06_07);
    gate_exe_wait_interruptible(GATE_INTRO_SCENE_LONG_WAIT_MS);
    gate_exe_show_caption(180, 10, 310, far_pointer(gate_ds_segment(), GATE_DS_INTRO_CAPTION_06));
    if (gate_exe_handle_intro_input() != 0)
        goto cleanup;
    gate_linked_show_intro_picture(GATE_INTRO_PICTURE_CAPTIONS_06_07, GATE_PICTURE_POSITION_ORIGIN);
    gate_exe_show_caption(180, 10, 310, far_pointer(gate_ds_segment(), GATE_DS_INTRO_CAPTION_07));
    if (gate_exe_handle_intro_input() != 0)
        goto cleanup;

    gate_linked_play_intro_delta_sequence(GATE_INTRO_PICTURE_CAPTIONS_17_18,
                                          GATE_INTRO_PICTURE_STARFIELD_FIRST_DELTA,
                                          GATE_INTRO_STARFIELD_PREVIEW_FRAMES);
    if (gate_exe_handle_intro_input() != 0)
        goto cleanup;
    gate_linked_play_intro_delta_sequence(GATE_INTRO_PICTURE_CAPTIONS_08_11,
                                          GATE_INTRO_PICTURE_STATION_LATER_DELTA,
                                          GATE_INTRO_STATION_LATER_FRAMES);
    gate_exe_wait_interruptible(GATE_INTRO_SCENE_SHORT_WAIT_MS);
    gate_exe_show_caption(130, 10, 310, far_pointer(gate_ds_segment(), GATE_DS_INTRO_CAPTION_08));
    if (gate_exe_handle_intro_input() != 0)
        goto cleanup;
    gate_linked_show_intro_picture(GATE_INTRO_PICTURE_CAPTIONS_08_11, GATE_PICTURE_POSITION_ORIGIN);
    gate_exe_show_caption(140, 10, 310, far_pointer(gate_ds_segment(), GATE_DS_INTRO_CAPTION_09));
    if (gate_exe_handle_intro_input() != 0)
        goto cleanup;
    gate_linked_show_intro_picture(GATE_INTRO_PICTURE_CAPTIONS_08_11, GATE_PICTURE_POSITION_ORIGIN);
    gate_exe_show_caption(140, 10, 310, far_pointer(gate_ds_segment(), GATE_DS_INTRO_CAPTION_10));
    if (gate_exe_handle_intro_input() != 0)
        goto cleanup;
    gate_linked_show_intro_picture(GATE_INTRO_PICTURE_CAPTIONS_08_11, GATE_PICTURE_POSITION_ORIGIN);
    gate_exe_show_caption(140, 10, 310, far_pointer(gate_ds_segment(), GATE_DS_INTRO_CAPTION_11));
    if (gate_exe_handle_intro_input() != 0)
        goto cleanup;

    gate_exe_fade_in_picture(GATE_INTRO_PICTURE_CAPTION_12);
    gate_exe_wait_interruptible(GATE_INTRO_SCENE_SHORT_WAIT_MS);
    if (gate_exe_handle_intro_input() != 0)
        goto cleanup;
    gate_exe_show_caption(10, 10, 140, far_pointer(gate_ds_segment(), GATE_DS_INTRO_CAPTION_12));
    if (gate_exe_handle_intro_input() != 0)
        goto cleanup;
    gate_exe_fade_in_picture(GATE_INTRO_PICTURE_CAPTIONS_13_14);
    gate_exe_show_caption(60, 120, 310, far_pointer(gate_ds_segment(), GATE_DS_INTRO_CAPTION_13));
    if (gate_exe_handle_intro_input() != 0)
        goto cleanup;
    gate_linked_show_intro_picture(GATE_INTRO_PICTURE_CAPTIONS_13_14, GATE_PICTURE_POSITION_ORIGIN);
    gate_exe_show_caption(140, 120, 310, far_pointer(gate_ds_segment(), GATE_DS_INTRO_CAPTION_14));
    if (gate_exe_handle_intro_input() != 0)
        goto cleanup;

    gate_exe_fade_in_picture(GATE_INTRO_PICTURE_CAPTION_15);
    gate_exe_wait_interruptible(GATE_INTRO_SCENE_SHORT_WAIT_MS);
    if (gate_exe_handle_intro_input() != 0)
        goto cleanup;
    gate_exe_show_caption(120, 10, 310, far_pointer(gate_ds_segment(), GATE_DS_INTRO_CAPTION_15));
    if (gate_exe_handle_intro_input() != 0)
        goto cleanup;
    gate_linked_play_intro_delta_sequence(GATE_INTRO_PICTURE_CAPTION_15,
                                          GATE_INTRO_PICTURE_APPROACH_FIRST_DELTA,
                                          GATE_INTRO_APPROACH_FRAMES);
    gate_linked_play_intro_delta_sequence(GATE_INTRO_PICTURE_CAPTIONS_08_11,
                                          GATE_INTRO_PICTURE_STATION_FIRST_DELTA,
                                          GATE_INTRO_STATION_FIRST_FRAMES);
    gate_linked_play_intro_delta_sequence(GATE_INTRO_PICTURE_EXIT_STARFIELD,
                                          GATE_INTRO_PICTURE_EXIT_SHIP_FIRST_DELTA,
                                          GATE_INTRO_EXIT_SHIP_FRAMES);
    gate_exe_wait_interruptible(GATE_INTRO_SCENE_LONG_WAIT_MS);
    if (gate_exe_handle_intro_input() != 0)
        goto cleanup;

    gate_exe_fade_in_picture(GATE_INTRO_PICTURE_CAPTION_16);
    gate_exe_wait_interruptible(GATE_INTRO_SCENE_LONG_WAIT_MS);
    if (gate_exe_handle_intro_input() != 0)
        goto cleanup;
    gate_exe_show_caption(10, 100, 160, far_pointer(gate_ds_segment(), GATE_DS_INTRO_CAPTION_16));
    if (gate_exe_handle_intro_input() != 0)
        goto cleanup;
    gate_exe_fade_in_picture(GATE_INTRO_PICTURE_CAPTIONS_17_18);
    gate_exe_wait_interruptible(GATE_INTRO_SCENE_SHORT_WAIT_MS);
    if (gate_exe_handle_intro_input() != 0)
        goto cleanup;
    gate_exe_show_caption(10, 10, 310, far_pointer(gate_ds_segment(), GATE_DS_INTRO_CAPTION_17));
    if (gate_exe_handle_intro_input() != 0)
        goto cleanup;
    gate_linked_show_intro_picture(GATE_INTRO_PICTURE_CAPTIONS_17_18, GATE_PICTURE_POSITION_ORIGIN);
    gate_exe_show_caption(10, 10, 310, far_pointer(gate_ds_segment(), GATE_DS_INTRO_CAPTION_18));
    gate_exe_handle_intro_input();  /* 0x1a05: return value discarded */

cleanup:
    gate_linked_fade_intro_palette_out();
finish:
    gate_linked_flush_keyboard();
}

/* GATE.EXE file 0x1a14..0x1c10. Intro title/palette interlude called from
 * 0x138a when the original display-mode word is nonzero. The four display
 * mode comparisons are separate loads, not a cached flag. Their JGE is
 * signed. No picture geometry or frame-capture timing was guessed here.
 *
 * In mode <2 only one palette byte changes per stage. In mode >=2 the
 * relevant triples fade 63..0, 0..63, 63..0, 63..0 in inclusive loops.
 * A pending key exits only after one of the three specified waits; unlike
 * 0x0e37 no key is consumed by this function.
 */
void gate_exe_play_title_interlude(void)
{
    int8_t i;

    for (i = 0; i < GATE_INTRO_TITLE_PALETTE_PREFIX_BYTES; ++i)
        gate_far_set_byte(gate_ds_word(GATE_DS_INTRO_PALETTE_SEGMENT),
                          (uint16_t)(GATE_INTRO_PALETTE_BANK_OFFSET + i),
                          GATE_INTRO_TITLE_MAX_DAC_LEVEL);
    gate_linked_fade_intro_palette_in();
    gate_exe_wait_interruptible(GATE_INTRO_TITLE_LEAD_IN_MS);
    gate_linked_show_intro_picture(GATE_INTRO_PICTURE_FIRST_SPLASH, GATE_PICTURE_POSITION_FIRST_PAIR);

    if ((int16_t)gate_far_word(gate_ds_word(GATE_DS_INTRO_MODE_SEGMENT), GATE_DS_GRAPHICS_MODE) < GATE_INTRO_RGB_PALETTE_MODE_START) {
        gate_far_set_byte(gate_ds_word(GATE_DS_INTRO_PALETTE_SEGMENT), GATE_INTRO_TITLE_FIRST_SINGLE_COMPONENT, 0);
        gate_linked_upload_intro_palette(runtime_palette_pointer());
    } else {
        for (i = GATE_INTRO_TITLE_MAX_DAC_LEVEL; i >= 0; --i) {
            gate_far_set_byte(gate_ds_word(GATE_DS_INTRO_PALETTE_SEGMENT), GATE_INTRO_TITLE_FIRST_RGB, (uint8_t)i);
            gate_far_set_byte(gate_ds_word(GATE_DS_INTRO_PALETTE_SEGMENT), (GATE_INTRO_TITLE_FIRST_RGB + GATE_INTRO_RGB_GREEN_COMPONENT), (uint8_t)i);
            gate_far_set_byte(gate_ds_word(GATE_DS_INTRO_PALETTE_SEGMENT), (GATE_INTRO_TITLE_FIRST_RGB + GATE_INTRO_RGB_BLUE_COMPONENT), (uint8_t)i);
            gate_linked_upload_intro_palette(runtime_palette_pointer());
        }
    }
    gate_exe_wait_interruptible(GATE_INTRO_TITLE_HOLD_MS);
    if (gate_linked_keyboard_pending() != 0)
        return;

    if ((int16_t)gate_far_word(gate_ds_word(GATE_DS_INTRO_MODE_SEGMENT), GATE_DS_GRAPHICS_MODE) < GATE_INTRO_RGB_PALETTE_MODE_START) {
        gate_far_set_byte(gate_ds_word(GATE_DS_INTRO_PALETTE_SEGMENT),
                          GATE_INTRO_TITLE_FIRST_SINGLE_COMPONENT, GATE_INTRO_TITLE_MAX_DAC_LEVEL);
        gate_linked_upload_intro_palette(runtime_palette_pointer());
    } else {
        for (i = 0; i <= GATE_INTRO_TITLE_MAX_DAC_LEVEL; ++i) {
            gate_far_set_byte(gate_ds_word(GATE_DS_INTRO_PALETTE_SEGMENT), GATE_INTRO_TITLE_FIRST_RGB, (uint8_t)i);
            gate_far_set_byte(gate_ds_word(GATE_DS_INTRO_PALETTE_SEGMENT), (GATE_INTRO_TITLE_FIRST_RGB + GATE_INTRO_RGB_GREEN_COMPONENT), (uint8_t)i);
            gate_far_set_byte(gate_ds_word(GATE_DS_INTRO_PALETTE_SEGMENT), (GATE_INTRO_TITLE_FIRST_RGB + GATE_INTRO_RGB_BLUE_COMPONENT), (uint8_t)i);
            gate_linked_upload_intro_palette(runtime_palette_pointer());
        }
    }
    gate_exe_wait_interruptible(GATE_INTRO_TITLE_MIDPOINT_MS);
    if (gate_linked_keyboard_pending() != 0)
        return;

    gate_linked_show_intro_picture(GATE_INTRO_PICTURE_SECOND_SPLASH, GATE_PICTURE_POSITION_FIRST_PAIR);
    if ((int16_t)gate_far_word(gate_ds_word(GATE_DS_INTRO_MODE_SEGMENT), GATE_DS_GRAPHICS_MODE) < GATE_INTRO_RGB_PALETTE_MODE_START) {
        gate_far_set_byte(gate_ds_word(GATE_DS_INTRO_PALETTE_SEGMENT), GATE_INTRO_TITLE_SECOND_SINGLE_COMPONENT, 0);
        gate_linked_upload_intro_palette(runtime_palette_pointer());
    } else {
        for (i = GATE_INTRO_TITLE_MAX_DAC_LEVEL; i >= 0; --i) {
            gate_far_set_byte(gate_ds_word(GATE_DS_INTRO_PALETTE_SEGMENT), GATE_INTRO_TITLE_SECOND_RGB, (uint8_t)i);
            gate_far_set_byte(gate_ds_word(GATE_DS_INTRO_PALETTE_SEGMENT), (GATE_INTRO_TITLE_SECOND_RGB + GATE_INTRO_RGB_GREEN_COMPONENT), (uint8_t)i);
            gate_far_set_byte(gate_ds_word(GATE_DS_INTRO_PALETTE_SEGMENT), (GATE_INTRO_TITLE_SECOND_RGB + GATE_INTRO_RGB_BLUE_COMPONENT), (uint8_t)i);
            gate_linked_upload_intro_palette(runtime_palette_pointer());
        }
    }
    gate_exe_wait_interruptible(GATE_INTRO_TITLE_HOLD_MS);
    if (gate_linked_keyboard_pending() != 0)
        return;

    if ((int16_t)gate_far_word(gate_ds_word(GATE_DS_INTRO_MODE_SEGMENT), GATE_DS_GRAPHICS_MODE) < GATE_INTRO_RGB_PALETTE_MODE_START) {
        gate_far_set_byte(gate_ds_word(GATE_DS_INTRO_PALETTE_SEGMENT), GATE_INTRO_TITLE_THIRD_SINGLE_COMPONENT, 0);
        gate_linked_upload_intro_palette(runtime_palette_pointer());
    } else {
        for (i = GATE_INTRO_TITLE_MAX_DAC_LEVEL; i >= 0; --i) {
            gate_far_set_byte(gate_ds_word(GATE_DS_INTRO_PALETTE_SEGMENT), GATE_INTRO_TITLE_THIRD_RGB, (uint8_t)i);
            gate_far_set_byte(gate_ds_word(GATE_DS_INTRO_PALETTE_SEGMENT), (GATE_INTRO_TITLE_THIRD_RGB + GATE_INTRO_RGB_GREEN_COMPONENT), (uint8_t)i);
            gate_far_set_byte(gate_ds_word(GATE_DS_INTRO_PALETTE_SEGMENT), (GATE_INTRO_TITLE_THIRD_RGB + GATE_INTRO_RGB_BLUE_COMPONENT), (uint8_t)i);
            gate_linked_upload_intro_palette(runtime_palette_pointer());
        }
    }
    gate_exe_wait_interruptible(GATE_INTRO_TITLE_HOLD_MS);
}
