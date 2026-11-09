/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Exercise the original stream cleanup in staged launcher M11.OBJ with
 * real DOS-owned DS and descriptor segments. This is the bounded quiet
 * branch only; active CRT stream close remains explicitly unsupported. */
#include "GATEMEM.H"
#include "RTLLAY.H"
#include <dos.h>
#include <stdio.h>

void gate_linked_shutdown_intro_stream(void);

static int check(uint16_t actual, uint16_t expected, const char *label)
{
    if (actual != expected) {
        printf("Stream probe: %s got %04x expected %04x\n", label, actual, expected);
        return 0;
    }
    return 1;
}

int main(void)
{
    unsigned data_segment = 0;
    unsigned descriptor_segment = 0;
    uint16_t offset = 0xfffdu; /* field +0ah wraps around the 16-bit offset */
    uint16_t flags_offset = 7u;
    GateFarPtr flush_stream;
    int valid = 1;

    if (_dos_allocmem(0x0620u, &data_segment) != 0) {
        puts("Stream probe: original DS window allocation failed");
        return 1;
    }
    gate_dos_bind_ds((uint16_t)data_segment);
    gate_ds_set_word(GATE_DS_CONTROL_STREAM_OPEN, 0xffffu);
    gate_ds_set_word(GATE_DS_CONTROL_STREAM_AUX, 0xabcdu);
    gate_ds_set_word(GATE_DS_CONTROL_STREAM_SEGMENT, 0u);
    gate_linked_shutdown_intro_stream();
    valid &= check(gate_ds_word(GATE_DS_CONTROL_STREAM_AUX), 0xabcdu,
                   "closed flag skips descriptor and preserves companion");

    if (_dos_allocmem(0x0020u, &descriptor_segment) != 0) {
        puts("Stream probe: descriptor allocation failed");
        return 1;
    }
    gate_ds_set_word(GATE_DS_CONTROL_STREAM_OFFSET, offset);
    gate_ds_set_word(GATE_DS_CONTROL_STREAM_SEGMENT, (uint16_t)descriptor_segment);

    gate_ds_set_word(GATE_DS_CONTROL_STREAM_OPEN, 0u);
    gate_far_set_byte((uint16_t)descriptor_segment, flags_offset, 0x04u);
    gate_linked_shutdown_intro_stream();
    valid &= check(gate_far_byte((uint16_t)descriptor_segment, flags_offset), 0u,
                   "inactive descriptor cleared");
    valid &= check(gate_ds_word(GATE_DS_CONTROL_STREAM_OPEN), 0xffffu,
                   "open flag invalidated");
    valid &= check(gate_ds_word(GATE_DS_CONTROL_STREAM_AUX), 0u,
                   "companion reset");

    gate_ds_set_word(GATE_DS_CONTROL_STREAM_OPEN, 0x7fffu);
    gate_ds_set_word(GATE_DS_CONTROL_STREAM_AUX, 0xfaceu);
    gate_far_set_byte((uint16_t)descriptor_segment, flags_offset, 0xc3u);
    gate_linked_shutdown_intro_stream();
    valid &= check(gate_far_byte((uint16_t)descriptor_segment, flags_offset), 0u,
                   "bypass descriptor cleared");
    valid &= check(gate_ds_word(GATE_DS_CONTROL_STREAM_OPEN), 0xffffu,
                   "bypass open flag invalidated");
    valid &= check(gate_ds_word(GATE_DS_CONTROL_STREAM_AUX), 0u,
                   "bypass companion reset");

    /* The nested RTL:6eb8 buffer-flush resets current FAR pointer and
     * pending count even when it does not need to call the CRT writer. */
    flush_stream.segment = (uint16_t)descriptor_segment;
    flush_stream.offset = 0x0010u;
    gate_far_set_byte(flush_stream.segment, 0x001au, 0x01u);
    gate_far_set_word(flush_stream.segment, 0x0010u, 0x1234u);
    gate_far_set_word(flush_stream.segment, 0x0012u, 0x5678u);
    gate_far_set_word(flush_stream.segment, 0x0014u, 0x9999u);
    gate_far_set_word(flush_stream.segment, 0x0016u, 0x4000u);
    gate_far_set_word(flush_stream.segment, 0x0018u, 0x5000u);
    valid &= check(gate_linked_flush_intro_stream(flush_stream), 0u,
                   "nonwriting mode flush result");
    valid &= check(gate_far_word(flush_stream.segment, 0x0010u), 0x4000u,
                   "nonwriting mode current offset reset");
    valid &= check(gate_far_word(flush_stream.segment, 0x0012u), 0x5000u,
                   "nonwriting mode current segment reset");
    valid &= check(gate_far_word(flush_stream.segment, 0x0014u), 0u,
                   "nonwriting mode count reset");
    valid &= check(gate_far_byte(flush_stream.segment, 0x001au), 0x01u,
                   "nonwriting mode flags unchanged");

    gate_far_set_byte(flush_stream.segment, 0x001au, 0x0au);
    gate_far_set_word(flush_stream.segment, 0x0010u, 0x3ffeu);
    gate_far_set_word(flush_stream.segment, 0x0014u, 0x7777u);
    valid &= check(gate_linked_flush_intro_stream(flush_stream), 0u,
                   "mode-2 no-pending flush result");
    valid &= check(gate_far_word(flush_stream.segment, 0x0010u), 0x4000u,
                   "mode-2 signed pending offset reset");
    valid &= check(gate_far_word(flush_stream.segment, 0x0014u), 0u,
                   "mode-2 pending count reset");
    puts(valid ? "Original stream no-write flush DOS probe: PASS" :
                 "Original stream no-write flush DOS probe: FAIL");

    valid &= check((uint16_t)_dos_freemem(descriptor_segment), 0u,
                   "release descriptor allocation");
    valid &= check((uint16_t)_dos_freemem(data_segment), 0u,
                   "release original DS window");
    puts(valid ? "Original stream quiet-close DOS probe: PASS" :
                 "Original stream quiet-close DOS probe: FAIL");
    return valid ? 0 : 1;
}
