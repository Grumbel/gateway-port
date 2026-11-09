/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Exercise the original stream cleanup in staged launcher M11.OBJ with
 * real DOS-owned DS and descriptor segments. This is the bounded quiet
 * branch only; active CRT stream close remains explicitly unsupported. */
#include "GATEMEM.H"
#include "GATEDOS.H"
#include "RTLLAY.H"
#include <dos.h>
#include <stdio.h>
#include <string.h>

void gate_linked_shutdown_intro_stream(void);
uint16_t gate_linked_write_crt_descriptor(uint16_t handle, GateFarPtr source,
                                          uint16_t count);

static int probe_crt_binary_write(void)
{
    char filename[11];
    unsigned char payload[7];
    unsigned char observed[7];
    GateFarPtr filename_pointer;
    GateFarPtr payload_pointer;
    GateFarPtr observed_pointer;
    GateFarPtr stream;
    GateDosFileResult io;
    FILE *existing;
    uint16_t handle;
    int valid = 1;

    strcpy(filename, "STRBIN.TMP");
    payload[0] = 0x11u;
    payload[1] = 0x22u;
    payload[2] = 0x33u;
    payload[3] = 0x44u;
    payload[4] = 0x55u;
    payload[5] = 0x66u;
    payload[6] = 0x77u;
    memset(observed, 0, sizeof observed);

    existing = fopen(filename, "rb");
    if (existing != NULL) {
        fclose(existing);
        puts("Stream binary probe: STRBIN.TMP already exists; refusing overwrite");
        return 0;
    }
    filename_pointer = gate_stack_pointer(filename);
    payload_pointer = gate_stack_pointer(payload);
    observed_pointer = gate_stack_pointer(observed);
    io = gate_dos_file_open(filename_pointer, 2u, 1u);
    if (io.carry) {
        puts("Stream binary probe: DOS file create failed");
        return 0;
    }
    handle = io.ax;
    gate_ds_set_word(GATE_DS_CRT_HANDLE_LIMIT, 20u);
    gate_far_set_byte(gate_ds_segment(),
                      (uint16_t)(GATE_DS_CRT_HANDLE_FLAGS + handle), 0u);
    valid &= check(gate_linked_write_crt_descriptor(handle, payload_pointer, 0u),
                   0u, "CRT empty binary write");
    valid &= check(gate_linked_write_crt_descriptor(handle, payload_pointer, 3u),
                   3u, "CRT binary first write");

    /* The original 7d81 append bit performs a seek to EOF before AH=40h.
     * Rewind first to detect a missing append seek. */
    io = gate_dos_file_seek(handle, 0u, 0ul);
    valid &= check(io.carry, 0u, "CRT append pre-rewind");
    gate_far_set_byte(gate_ds_segment(),
        (uint16_t)(GATE_DS_CRT_HANDLE_FLAGS + handle), GATE_CRT_APPEND_BIT);
    payload_pointer.offset = (uint16_t)(payload_pointer.offset + 3u);
    valid &= check(gate_linked_write_crt_descriptor(handle, payload_pointer, 2u),
                   2u, "CRT binary append write");

    /* Exercise RTL:6f0f's caller with an actual stream descriptor. */
    stream.segment = gate_ds_segment();
    stream.offset = (uint16_t)(GATE_DS_CRT_STREAMS + GATE_CRT_STREAM_STRIDE);
    gate_far_set_word(stream.segment, stream.offset,
                      (uint16_t)(payload_pointer.offset + 4u));
    gate_far_set_word(stream.segment, (uint16_t)(stream.offset + 4u), 2u);
    gate_far_set_word(stream.segment, (uint16_t)(stream.offset + 6u),
                      (uint16_t)(payload_pointer.offset + 2u));
    gate_far_set_word(stream.segment, (uint16_t)(stream.offset + 8u),
                      payload_pointer.segment);
    gate_far_set_byte(stream.segment, (uint16_t)(stream.offset + 10u), 0x02u);
    gate_far_set_byte(stream.segment, (uint16_t)(stream.offset + 11u),
                      (uint8_t)handle);
    gate_far_set_byte(stream.segment,
        (uint16_t)(GATE_DS_CRT_DESCRIPTOR_FLAGS + GATE_CRT_FILE_STRIDE), 1u);
    valid &= check(gate_linked_flush_intro_stream(stream), 0u,
                   "CRT binary stream flush result");
    valid &= check(gate_far_word(stream.segment, stream.offset),
                   (uint16_t)(payload_pointer.offset + 2u),
                   "CRT binary stream current reset");
    valid &= check(gate_far_word(stream.segment, (uint16_t)(stream.offset + 4u)),
                   0u, "CRT binary stream pending reset");

    io = gate_dos_file_seek(handle, 0u, 0ul);
    valid &= check(io.carry, 0u, "CRT binary verify rewind");
    io = gate_dos_file_read(handle, observed_pointer, 7u);
    valid &= check(io.carry, 0u, "CRT binary verify read carry");
    valid &= check(io.ax, 7u, "CRT binary verify read length");
    valid &= check(memcmp(payload, observed, 7u) == 0, 1u,
                   "CRT binary verify exact seven bytes");
    io = gate_dos_file_close(handle);
    valid &= check(io.carry, 0u, "CRT binary close");
    valid &= check((uint16_t)remove(filename), 0u, "CRT binary remove probe file");
    puts(valid ? "Original CRT binary descriptor DOS probe: PASS" :
                 "Original CRT binary descriptor DOS probe: FAIL");
    return valid;
}

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
    GateFarPtr table_stream;
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

    /* Original RTL:6ed8/6eea relocated separately to DS:3244/3334.
     * Stream #1 sits at base+12, with its file metadata at base+6.
     * A clear metadata bit zero must bypass writing even when bytes
     * appear pending, while a set bit goes through the signed cursor
     * comparison before deciding whether a CRT write is necessary. */
    table_stream.segment = (uint16_t)data_segment;
    table_stream.offset = (uint16_t)(GATE_DS_CRT_STREAMS +
                                    GATE_CRT_STREAM_STRIDE);
    gate_far_set_word(table_stream.segment, table_stream.offset, 0x1010u);
    gate_far_set_word(table_stream.segment, (uint16_t)(table_stream.offset + 2u),
                      0x8888u);
    gate_far_set_word(table_stream.segment, (uint16_t)(table_stream.offset + 4u),
                      0x1234u);
    gate_far_set_word(table_stream.segment, (uint16_t)(table_stream.offset + 6u),
                      0x1000u);
    gate_far_set_word(table_stream.segment, (uint16_t)(table_stream.offset + 8u),
                      0x2222u);
    gate_far_set_byte(table_stream.segment, (uint16_t)(table_stream.offset + 10u),
                      0x02u);
    gate_far_set_byte(table_stream.segment,
                      (uint16_t)(GATE_DS_CRT_DESCRIPTOR_FLAGS +
                      GATE_CRT_FILE_STRIDE), 0u);
    valid &= check(gate_linked_flush_intro_stream(table_stream), 0u,
                   "mode-2 metadata-clear flush result");
    valid &= check(gate_far_word(table_stream.segment, table_stream.offset), 0x1000u,
                   "mode-2 metadata-clear pointer reset");
    valid &= check(gate_far_word(table_stream.segment,
                   (uint16_t)(table_stream.offset + 2u)), 0x2222u,
                   "mode-2 metadata-clear pointer segment reset");
    valid &= check(gate_far_word(table_stream.segment,
                   (uint16_t)(table_stream.offset + 4u)), 0u,
                   "mode-2 metadata-clear pending reset");

    gate_far_set_byte(table_stream.segment,
                      (uint16_t)(GATE_DS_CRT_DESCRIPTOR_FLAGS +
                      GATE_CRT_FILE_STRIDE), 1u);
    gate_far_set_word(table_stream.segment, table_stream.offset, 0x0ffeu);
    gate_far_set_word(table_stream.segment, (uint16_t)(table_stream.offset + 4u),
                      0x9999u);
    valid &= check(gate_linked_flush_intro_stream(table_stream), 0u,
                   "mode-2 metadata-set nonpositive pending flush result");
    valid &= check(gate_far_word(table_stream.segment, table_stream.offset), 0x1000u,
                   "mode-2 metadata-set pointer reset");
    valid &= check(gate_far_word(table_stream.segment,
                   (uint16_t)(table_stream.offset + 4u)), 0u,
                   "mode-2 metadata-set pending reset");
    puts(valid ? "Original stream descriptor-table DOS probe: PASS" :
                 "Original stream descriptor-table DOS probe: FAIL");

    valid &= probe_crt_binary_write();

    valid &= check((uint16_t)_dos_freemem(descriptor_segment), 0u,
                   "release descriptor allocation");
    valid &= check((uint16_t)_dos_freemem(data_segment), 0u,
                   "release original DS window");
    puts(valid ? "Original stream quiet-close DOS probe: PASS" :
                 "Original stream quiet-close DOS probe: FAIL");
    return valid ? 0 : 1;
}
