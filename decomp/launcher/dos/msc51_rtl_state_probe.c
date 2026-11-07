/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Compile and execute against the *same* RTL accessor source as the native
 * intro. The original DOS DS selector words must remain live, not cached. */
#include "RTLSTATE.H"
#include "RTLLAY.H"
#include "RTLIMAC.H"
#include <dos.h>
#include <stdio.h>

static int check(uint16_t actual, uint16_t expected, const char *what)
{
    if (actual != expected) {
        printf("RTL state probe: %s got %04x expected %04x\n",
               what, actual, expected);
        return 0;
    }
    return 1;
}

int main(void)
{
    unsigned int ds = 0;
    unsigned int mode = 0;
    GateFarPtr pointer;
    int valid = 1;

    if (_dos_allocmem(0x1000u, &ds) != 0) {
        puts("RTL state probe: guest DS allocation failed");
        return 1;
    }
    if (_dos_allocmem(0x0100u, &mode) != 0) {
        puts("RTL state probe: mode segment allocation failed");
        _dos_freemem(ds);
        return 1;
    }
    gate_dos_bind_ds((uint16_t)ds);

    /* Each member is a live segment selector in the linked launcher's DS.
     * Different values ensure the getters cannot accidentally be aliases. */
    gate_ds_set_word(GATE_DS_IMAGE_MODE_SEGMENT, 0x1111u);
    gate_ds_set_word(GATE_DS_IMAGE_PALETTE_SEGMENT, 0x2222u);
    gate_ds_set_word(GATE_DS_IMAGE_FLAGS_SEGMENT, 0x3333u);
    gate_ds_set_word(GATE_DS_IMAGE_STATUS_SEGMENT, 0x4444u);
    gate_ds_set_word(GATE_DS_RESOURCE_HANDLE_SEGMENT, 0x5555u);
    gate_ds_set_word(GATE_DS_IMAGE_HEADER_SEGMENT, 0x6666u);
    gate_ds_set_word(GATE_DS_RESOURCE_SIZE_TABLE_SEGMENT, 0x7777u);
    gate_ds_set_word(GATE_DS_IMAGE_TRANSFORM_SEGMENT, 0x8888u);
    gate_ds_set_word(GATE_DS_IMAGE_PREPARE_DEST_SEGMENT, 0x9999u);
    gate_ds_set_word(GATE_DS_ANIMATION_FIELD_22_SEGMENT, 0xaaaau);
    gate_ds_set_word(GATE_DS_ANIMATION_FIELD_24_SEGMENT, 0xbbbbu);
    gate_ds_set_word(GATE_DS_ANIMATION_FIELD_26_SEGMENT, 0xccccu);
    valid &= check(gate_rtl_image_mode_segment(), 0x1111u, "image mode selector");
    valid &= check(gate_rtl_image_palette_segment(), 0x2222u, "palette selector");
    valid &= check(gate_rtl_preparer_flag_source_segment(), 0x3333u, "flags selector");
    valid &= check(gate_rtl_preparer_status_segment(), 0x4444u, "status selector");
    valid &= check(gate_rtl_resource_handle_segment(), 0x5555u, "handle selector");
    valid &= check(gate_rtl_image_header_segment(), 0x6666u, "header selector");
    valid &= check(gate_rtl_resource_size_table_segment(), 0x7777u, "sizes selector");
    valid &= check(gate_rtl_image_transform_segment(), 0x8888u, "transform selector");
    valid &= check(gate_rtl_preparer_word_dest_segment(), 0x9999u, "destination selector");
    valid &= check(gate_rtl_anim_segment_22(), 0xaaaau, "animation field 22");
    valid &= check(gate_rtl_anim_segment_24(), 0xbbbbu, "animation field 24");
    valid &= check(gate_rtl_anim_segment_26(), 0xccccu, "animation field 26");
    pointer = gate_rtl_image_palette_source();
    valid &= check(pointer.segment, 0x8888u, "palette source segment");
    valid &= check(pointer.offset, GATE_INTRO_PALETTE_BANK_OFFSET, "palette source offset");
    gate_ds_set_word(GATE_DS_IMAGE_MODE_SEGMENT, 0xabcdu);
    valid &= check(gate_rtl_image_mode_segment(), 0xabcdu, "selector remains live");

    gate_far_set_byte((uint16_t)ds, 0x1400u, 0x5au);
    valid &= check(gate_rtl_image_data_segment(), (uint16_t)ds, "image data DS");
    valid &= check(gate_rtl_image_data_byte(0x1400u), 0x5au, "image data byte");
    gate_rtl_resource_ds_set_word(0x000eu, 0x9a47u);
    valid &= check(gate_rtl_resource_ds_word(0x000eu), 0x9a47u, "resource scalar round trip");
    valid &= check(gate_ds_word(GATE_DS_RESOURCE_SCALARS_BASE + 0x000eu),
                   0x9a47u, "resource scalar DS offset");

    gate_ds_set_word(GATE_DS_ACTIVE_FONT_SEGMENT, 0x1234u);
    gate_ds_set_word(GATE_DS_ACTIVE_FONT_OFFSET, 0x5678u);
    pointer = gate_rtl_active_font();
    valid &= check(pointer.segment, 0x1234u, "active font segment");
    valid &= check(pointer.offset, 0x5678u, "active font offset");
    gate_rtl_set_cursor(-8, 319);
    valid &= check((uint16_t)gate_rtl_cursor_x(), 0xfff8u, "signed cursor x");
    valid &= check((uint16_t)gate_rtl_cursor_y(), 319u, "cursor y");
    valid &= check(gate_ds_word(GATE_DS_TEXT_CURSOR_X), 0xfff8u, "cursor DS x");
    gate_rtl_set_colors(0x0043u, -1);
    valid &= check(gate_rtl_ink_color(), 0x0043u, "ink");
    valid &= check((uint16_t)gate_rtl_background_color(), 0xffffu, "signed background");
    valid &= check(gate_ds_word(GATE_DS_TEXT_BACKGROUND_COLOR), 0xffffu,
                   "background DS");

    gate_ds_set_word(GATE_DS_FONT_TAB_ADVANCE_SEGMENT, (uint16_t)mode);
    gate_ds_set_word(GATE_DS_FONT_LINE_PITCH_SEGMENT, (uint16_t)mode);
    gate_ds_set_word(GATE_DS_FONT_TAB_LIMIT_SEGMENT, (uint16_t)mode);
    gate_ds_set_word(GATE_DS_FONT_HEIGHT_SEGMENT, (uint16_t)mode);
    gate_far_set_word((uint16_t)mode, GATE_MODE_DESC_TAB_ADVANCE, 19u);
    gate_far_set_word((uint16_t)mode, GATE_MODE_DESC_TEXT_LINE_PITCH, 80u);
    gate_far_set_word((uint16_t)mode, GATE_MODE_DESC_SCREEN_WIDTH, 640u);
    gate_far_set_word((uint16_t)mode, GATE_MODE_DESC_FONT_HEIGHT, 16u);
    valid &= check(gate_rtl_tab_advance(), 19u, "tab advance");
    valid &= check((uint16_t)gate_rtl_tab_pitch(), 19u, "tab pitch");
    valid &= check((uint16_t)gate_rtl_text_mode_pitch(), 80u, "line pitch");
    valid &= check((uint16_t)gate_rtl_tab_limit(), 640u, "tab limit");
    valid &= check((uint16_t)gate_rtl_newline_pitch(), 16u, "newline pitch");
    gate_far_set_word((uint16_t)mode, GATE_MODE_DESC_TAB_ADVANCE, 23u);
    valid &= check(gate_rtl_tab_advance(), 23u, "mode field remains live");

    gate_ds_set_word(GATE_DS_GRAPHICS_MODE, 0x0003u);
    gate_ds_set_word(GATE_DS_VIDEO_ADAPTER_TYPE, 5u);
    gate_ds_set_word(GATE_DS_VGA_STATUS_PORT, 0x03dau);
    gate_ds_set_word(GATE_DS_PALETTE_WORK_BASE + 0x000au, 0xfffeu);
    gate_ds_set_word(GATE_DS_PALETTE_CONVERSION_SEGMENT, (uint16_t)mode);
    gate_far_set_word((uint16_t)mode, GATE_MODE_DESC_PALETTE_CONVERSION, 0x1234u);
    gate_far_set_byte((uint16_t)ds, GATE_DS_PALETTE_WORK_BASE + 0x035cu, 0x5bu);
    valid &= check(gate_rtl_palette_mode(), 3u, "palette mode");
    valid &= check(gate_rtl_palette_video_type(), 5u, "palette adapter type");
    valid &= check(gate_rtl_palette_status_port(), 0x03dau, "status port word");
    valid &= check((uint16_t)gate_rtl_palette_color_count(0x000au), 0xfffeu,
                   "signed palette color count");
    valid &= check(gate_rtl_palette_conversion(), 0x1234u, "palette conversion");
    valid &= check(gate_rtl_palette_table_byte(0x035cu), 0x5bu, "palette table byte");

    /* The graphics helper uses the selector words already tested above,
     * but dereferences fields in their selected segments. Rebind them to
     * our real allocated DOS segment for the dereference probe. */
    gate_ds_set_word(GATE_DS_IMAGE_MODE_SEGMENT, (uint16_t)mode);
    gate_ds_set_word(GATE_DS_IMAGE_STATUS_SEGMENT, (uint16_t)mode);
    gate_ds_set_word(GATE_DS_RESOURCE_SIZE_TABLE_SEGMENT, (uint16_t)mode);
    gate_ds_set_word(GATE_DS_IMAGE_TRANSFORM_SEGMENT, (uint16_t)mode);
    gate_far_set_word((uint16_t)mode, GATE_RTL_VIDEO_MODE_OFFSET, 7u);
    gate_far_set_word((uint16_t)mode, GATE_RTL_SIZE_TABLE_OFFSET + 4u, 0xfffdu);
    gate_far_set_byte((uint16_t)mode, GATE_RTL_TRANSFORM_OFFSET + 3u, 0x42u);
    valid &= check(gate_rtl_graphics_mode(), 7u, "graphics mode dereference");
    gate_rtl_graphics_set_index(0x6789u);
    valid &= check(gate_far_word((uint16_t)mode, GATE_PREPARE_STATUS_WORD),
                   0x6789u, "graphics index write");
    valid &= check((uint16_t)gate_rtl_graphics_byte_count(4u), 0xfffdu,
                   "graphics byte count");
    valid &= check(gate_rtl_graphics_byte(3u), 0x42u, "graphics byte read");
    gate_rtl_graphics_set_byte(3u, 0x99u);
    valid &= check(gate_far_byte((uint16_t)mode, GATE_RTL_TRANSFORM_OFFSET + 3u),
                   0x99u, "graphics byte write");

    if (_dos_freemem(mode) != 0 || _dos_freemem(ds) != 0) {
        puts("RTL state probe: DOS free failed");
        return 1;
    }
    puts(valid ? "RTL state probe: PASS" : "RTL state probe: FAIL");
    return valid ? 0 : 1;
}
