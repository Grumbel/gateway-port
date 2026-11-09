/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_rtl_file.h"
#include "gate_dos_file_runtime.h"
#include <stdlib.h>

/* Original RTL:7d64..7eab, linked in the verified 02a5 resident bank.
 * RTL:7d6f, 7d7c and 7d8e independently relocate to DS:2bee/2bf0/2bf0.
 * For binary-mode writes and text writes containing no LF, the only OS
 * services are DOS AH=42h (append) and AH=40h (write). RTL:7db3..7db7
 * scans the whole text source with REPNE SCASB; if it finds no LF, the
 * original jumps to the ordinary writer at RTL:7e74. That path returns
 * 0 for an empty write; RTL:7e91 returns the DOS byte count for a successful
 * nonempty write. A zero-byte
 * result on a device is accepted only if the first source byte is 1ah.
 *
 * Original CRT errno/CF translation still requires reconstruction. Reject
 * failed DOS calls before inventing any error-global side effect. */

/* RTL:7e18..7e38 flushes a nonempty SS buffer and accumulates AX even
 * for short positive DOS writes; a zero or carry result requires the
 * still-unrecovered CRT error path. */
static void gate_crt_flush_text_buffer(uint16_t handle, GateFarPtr source,
                                       uint16_t *pending, uint16_t *written)
{
    GateDosFileResult result;
    if (*pending == 0)
        return;
    result = gate_dos_file_write(handle, source, *pending);
    if (result.carry || result.ax == 0)
        abort();
    *written = (uint16_t)(*written + result.ax);
    *pending = 0;
}

/* RTL:7dc3..7e6e uses a 128-byte SS buffer for stack space in
 * (0a8h,0228h) and a 512-byte SS buffer for at least 0228h bytes.
 * The C89 compiler has a different frame size, so require a deliberately
 * larger margin (0180h or 0300h respectively), then check the actual
 * allocated SS buffer remains above the original stack limit.
 *
 * RTL:7de3..7e08 emits LF as CR LF and flushes a full buffer BEFORE the
 * next byte. RTL:7e18..7e38 resets DI to the buffer start. RTL:7e63
 * returns the total DOS count minus inserted CR bytes (modulo 16 bits).
 * Original carry/zero-result errno paths still abort. */
static uint16_t gate_crt_write_text_lf_buffer(uint16_t handle,
        GateFarPtr source, uint16_t count, uint8_t *buffer, uint16_t capacity)
{
    GateFarPtr stack_buffer;
    uint16_t stack_limit = gate_ds_word(GATE_DS_CRT_STACK_LIMIT);
    uint16_t offset = source.offset;
    uint16_t pending = 0;
    uint16_t inserted = 0;
    uint16_t written = 0;
    uint16_t remaining = count;

    stack_buffer = gate_stack_pointer(buffer);

    /* The hardware stack grows downward; do not call DOS from a buffer
     * which would overlap the original linked CRT stack guard. */
    if (stack_buffer.offset <= stack_limit ||
        (uint16_t)(stack_buffer.offset - stack_limit) < 0x80u)
        abort();

    while (remaining != 0) {
        uint8_t byte = gate_far_byte(source.segment, offset);
        offset = (uint16_t)(offset + 1u);
        if (byte == 0x0au) {
            if (pending == capacity)
                gate_crt_flush_text_buffer(handle, stack_buffer, &pending,
                                            &written);
            buffer[pending++] = 0x0du;
            ++inserted;
        }
        if (pending == capacity)
            gate_crt_flush_text_buffer(handle, stack_buffer, &pending,
                                        &written);
        buffer[pending++] = byte;
        --remaining;
    }
    gate_crt_flush_text_buffer(handle, stack_buffer, &pending, &written);
    return (uint16_t)(written - inserted);
}

static uint16_t gate_crt_write_text_lf_128(uint16_t handle,
                                           GateFarPtr source, uint16_t count)
{
    uint8_t buffer[128];
    return gate_crt_write_text_lf_buffer(handle, source, count,
                                          buffer, (uint16_t)sizeof buffer);
}

static uint16_t gate_crt_write_text_lf_512(uint16_t handle,
                                           GateFarPtr source, uint16_t count)
{
    uint8_t buffer[512];
    return gate_crt_write_text_lf_buffer(handle, source, count,
                                          buffer, (uint16_t)sizeof buffer);
}

uint16_t gate_linked_write_crt_descriptor(uint16_t handle, GateFarPtr source,
                                          uint16_t count)
{
    uint8_t flags;
    uint8_t has_lf = 0;
    GateDosFileResult result;

    if (handle >= gate_ds_word(GATE_DS_CRT_HANDLE_LIMIT))
        abort();
    flags = gate_far_byte(gate_ds_segment(),
                          (uint16_t)(GATE_DS_CRT_HANDLE_FLAGS + handle));
    if ((flags & GATE_CRT_TEXT_BIT) != 0) {
        /* RTL:7da7..7db7: ES:DI scans count bytes for LF, with DI
         * wrapping at 16 bits. No match falls through to raw AH=40h. */
        uint16_t remaining = count;
        uint16_t offset = source.offset;
        while (remaining != 0) {
            if (gate_far_byte(source.segment, offset) == 0x0au) {
                has_lf = 1;
                break;
            }
            offset = (uint16_t)(offset + 1u);
            --remaining;
        }
    }
    if ((flags & GATE_CRT_APPEND_BIT) != 0) {
        result = gate_dos_file_seek(handle, 2u, 0ul);
        if (result.carry)
            abort();
    }
    if (count == 0)
        return 0;

    if (has_lf) {
        uint8_t stack_probe;
        GateFarPtr probe;
        uint16_t limit = gate_ds_word(GATE_DS_CRT_STACK_LIMIT);
        uint16_t available;
        probe = gate_stack_pointer(&stack_probe);
        if (probe.offset <= limit)
            abort();
        available = (uint16_t)(probe.offset - limit);
        if (available >= 0x0228u) {
            if (available < 0x0300u)
                abort();
            return gate_crt_write_text_lf_512(handle, source, count);
        }
        if (available < 0x0180u)
            abort();
        return gate_crt_write_text_lf_128(handle, source, count);
    }

    result = gate_dos_file_write(handle, source, count);
    if (result.carry)
        abort();
    if (result.ax != 0)
        return result.ax;
    if ((flags & GATE_CRT_DEVICE_BIT) != 0 &&
        gate_far_byte(source.segment, source.offset) == GATE_CRT_DOS_CTRL_Z)
        return 0;
    abort();
}

/* Nested RTL:6eb8..6f45, linked from the first control-key CRT close.
 * On EVERY return the original writes the descriptor base FAR pointer
 * into current (+0/+2) and resets its pending count (+4), even if no
 * write was needed. The mode-2 path without bit 08h indexes a relocated
 * DOS descriptor flag table. Positive pending bytes use RTL:7d64's
 * bounded CRT writer; a short write sets descriptor error bit 20h and
 * makes the result ffffh. */
uint16_t gate_linked_flush_intro_stream(GateFarPtr stream)
{
    uint16_t field;
    uint8_t flags;
    uint16_t base_offset;
    uint16_t base_segment;
    uint16_t status = 0;

    if (stream.segment == 0)
        abort();
    field = (uint16_t)(stream.offset + GATE_CONTROL_STREAM_FLAGS_OFFSET);
    flags = gate_far_byte(stream.segment, field);
    if ((flags & GATE_CONTROL_STREAM_MODE_MASK) == GATE_CONTROL_STREAM_WRITE_MODE) {
        uint8_t needs_buffer_check = 1;
        if ((flags & GATE_CONTROL_STREAM_FORCE_WRITE_BIT) == 0) {
            /* RTL:6ed5..6eed: original signed IDIV by 12 truncates toward
             * zero, even for an unaligned or negative descriptor offset.
             * Convert the 16-bit quotient back to the 16-bit BX index
             * before testing the first byte of its six-byte file record. */
            int16_t delta = (int16_t)(uint16_t)(stream.offset - GATE_DS_CRT_STREAMS);
            int16_t index = (int16_t)(delta / GATE_CRT_STREAM_STRIDE);
            uint16_t flag_address = (uint16_t)(GATE_DS_CRT_DESCRIPTOR_FLAGS +
                     (uint16_t)(index * GATE_CRT_FILE_STRIDE));
            if ((gate_far_byte(gate_ds_segment(), flag_address) &
                 GATE_CRT_FILE_WRITE_BIT) == 0)
                needs_buffer_check = 0;
        }
        if (needs_buffer_check) {
            uint16_t cursor = gate_far_word(stream.segment, (uint16_t)(stream.offset +
                                            GATE_CONTROL_STREAM_CURRENT_OFFSET));
            uint16_t base = gate_far_word(stream.segment, (uint16_t)(stream.offset +
                                          GATE_CONTROL_STREAM_BASE_OFFSET));
            uint16_t pending = (uint16_t)(cursor - base);
            if ((int16_t)pending > 0) {
                GateFarPtr source;
                uint16_t written;
                source.offset = base;
                source.segment = gate_far_word(stream.segment,
                    (uint16_t)(stream.offset + GATE_CONTROL_STREAM_BASE_SEGMENT));
                written = gate_linked_write_crt_descriptor(
                    (uint16_t)(int16_t)(int8_t)gate_far_byte(stream.segment,
                         (uint16_t)(stream.offset + GATE_CONTROL_STREAM_HANDLE_OFFSET)), source, pending);
                if (written != pending) {
                    gate_far_set_byte(stream.segment, field,
                                      (uint8_t)(flags | 0x20u));
                    status = 0xffffu;
                }
            }
        }
    }

    base_offset = gate_far_word(stream.segment, (uint16_t)(stream.offset +
                                GATE_CONTROL_STREAM_BASE_OFFSET));
    base_segment = gate_far_word(stream.segment, (uint16_t)(stream.offset +
                                 GATE_CONTROL_STREAM_BASE_SEGMENT));
    gate_far_set_word(stream.segment, (uint16_t)(stream.offset +
                      GATE_CONTROL_STREAM_CURRENT_OFFSET), base_offset);
    gate_far_set_word(stream.segment, (uint16_t)(stream.offset +
                      GATE_CONTROL_STREAM_CURRENT_SEGMENT), base_segment);
    gate_far_set_word(stream.segment, (uint16_t)(stream.offset +
                      GATE_CONTROL_STREAM_PENDING_COUNT), 0);
    return status;
}

/* First Ctrl-Break FAR callee, RTL:15706..15729, linked 221b:021f.
 * DS:156e/1570 hold a signed open flag and companion word; DS:6150/6152
 * hold the stream descriptor FAR pointer (a distinct relocation group).
 * Nested RTL:6640 clears byte +0a without I/O if (flags & 83h) == 0 or
 * (flags & 40h) != 0. Its active route first calls RTL:6eb8 to flush,
 * then RTL:6cd2 to release the buffer (a no-op unless bit 08h is set),
 * then RTL:7a30 to close the signed-byte handle from descriptor +0bh.
 * These FAR targets are visible as 12a5:17da, :15f4 and :2352 in the
 * 500ms original RAM capture. The successful CRT close resets the linked
 * DS:2bf0 handle flag; the parent only post-processes if the six-byte
 * file record's +4 WORD is nonzero. Only the no-buffer/no-postprocess,
 * successful-flush-and-close branch is reconstructed. Heap and post-close
 * branches abort before mutation; DOS errors abort without fabricating
 * the original CRT errno side effects. */
void gate_linked_shutdown_intro_stream(void)
{
    GateFarPtr stream;
    uint8_t flags;
    if ((int16_t)gate_ds_word(GATE_DS_CONTROL_STREAM_OPEN) < 0)
        return;

    stream.offset = gate_ds_word(GATE_DS_CONTROL_STREAM_OFFSET);
    stream.segment = gate_ds_word(GATE_DS_CONTROL_STREAM_SEGMENT);
    if (stream.segment == 0)
        abort();
    flags = gate_far_byte(stream.segment,
                          (uint16_t)(stream.offset + GATE_CONTROL_STREAM_FLAGS_OFFSET));
    if ((flags & GATE_CONTROL_STREAM_ACTIVE_MASK) != 0 &&
        (flags & GATE_CONTROL_STREAM_BYPASS_BIT) == 0) {
        int16_t delta;
        int16_t index;
        uint16_t file_record;
        uint16_t handle;
        GateDosFileResult close_result;

        /* RTL:668e's buffer-release call does nothing without bit 08h.
         * Leave the heap-owned branch untouched until its allocator is
         * understood in the original runtime's DOS context. */
        if ((flags & GATE_CONTROL_STREAM_FORCE_WRITE_BIT) != 0)
            abort();

        delta = (int16_t)(uint16_t)(stream.offset - GATE_DS_CRT_STREAMS);
        index = (int16_t)(delta / GATE_CRT_STREAM_STRIDE);
        file_record = (uint16_t)(GATE_DS_CRT_DESCRIPTOR_FLAGS +
            (uint16_t)(index * GATE_CRT_FILE_STRIDE));
        if (gate_far_word(gate_ds_segment(), (uint16_t)(file_record +
                    GATE_CRT_FILE_CLOSE_EXTRA_WORD_OFFSET)) != 0)
            abort();

        handle = (uint16_t)(int16_t)(int8_t)gate_far_byte(stream.segment,
            (uint16_t)(stream.offset + GATE_CONTROL_STREAM_HANDLE_OFFSET));
        if (handle >= gate_ds_word(GATE_DS_CRT_HANDLE_LIMIT))
            abort();
        if (gate_linked_flush_intro_stream(stream) != 0)
            abort();
        close_result = gate_dos_file_close(handle);
        if (close_result.carry)
            abort();
        gate_far_set_byte(gate_ds_segment(),
            (uint16_t)(GATE_DS_CRT_HANDLE_FLAGS + handle), 0u);
    }

    gate_far_set_byte(stream.segment,
                      (uint16_t)(stream.offset + GATE_CONTROL_STREAM_FLAGS_OFFSET), 0);
    gate_ds_set_word(GATE_DS_CONTROL_STREAM_OPEN, 0xffffu);
    gate_ds_set_word(GATE_DS_CONTROL_STREAM_AUX, 0);
}

/* GATE.RTL:0xe4ff..0xe510, 0c33:000e.  Keep the source's order of both
 * DS global writes.  The matching original caller may set this base once. */
void gate_rtl_set_resource_path_base(GateFarPtr base)
{
    gate_ds_set_word(GATE_DS_RESOURCE_PATH_BASE_OFFSET, base.offset);
    gate_ds_set_word(GATE_DS_RESOURCE_PATH_BASE_SEGMENT, base.segment);
}

/* GATE.RTL:0xe511..0xe530, 0c33:0020.  This is specifically an unsigned
 * AX < 1 test, i.e. one only when the lower-level result is zero. */
uint16_t gate_rtl_check_resource_path(GateFarPtr path)
{
    return gate_linked_open_runtime_path(path, GATE_RESOURCE_PATH_PROBE_CODE) == 0 ? 1u : 0u;
}

/* GATE.RTL:0xe531..0xe581, 0c33:0040.  Original stack layout is an
 * 80-byte pathname and a separate result WORD; the formatter's nine pushed
 * WORDs are preserved in source-argument order.  Input `type` is multiplied
 * by four with 16-bit truncation before indexing the original DS table. */
uint16_t gate_rtl_open_resource_file(uint16_t type, uint16_t resource)
{
    uint8_t path[GATE_RESOURCE_PATH_STACK_BYTES];
    uint16_t entry = (uint16_t)(GATE_DS_RESOURCE_PATH_TABLE + (uint16_t)(type << GATE_RESOURCE_PATH_TABLE_ENTRY_SHIFT));
    uint16_t ds = gate_ds_segment();
    GateFarPtr table;
    GateFarPtr base;
    GateFarPtr format;
    GateFarPtr pathname;

    table.segment = gate_ds_word((uint16_t)(entry + GATE_RESOURCE_PATH_TABLE_SEGMENT_WORD_OFFSET));
    table.offset = gate_ds_word(entry);
    base.segment = gate_ds_word(GATE_DS_RESOURCE_PATH_BASE_SEGMENT);
    base.offset = gate_ds_word(GATE_DS_RESOURCE_PATH_BASE_OFFSET);
    format.segment = ds;
    format.offset = GATE_DS_RESOURCE_PATH_FORMAT;
    pathname = gate_stack_pointer(path);

    gate_linked_format_resource_filename(pathname, format, base, resource, table);
    return gate_linked_dos_file_open(pathname, type == GATE_RESOURCE_FILE_CREATE_TYPE ? GATE_DOS_FILE_CREATE_FLAG : 0u);
}

/* GATE.RTL:0xe582..0xe59a, 0c33:0091.  The guest pushes the offset's high
 * WORD, low WORD, zero origin, then handle.  The return value is ignored. */
void gate_rtl_seek_resource_file(uint16_t handle, uint32_t offset)
{
    (void)gate_linked_dos_file_seek(handle, 0, offset);
}

/* GATE.RTL:0xe59b..0xe5c4, 0c33:00aa.  The byte is read into a WORD-sized
 * stack slot, so the top byte can remain from the original entry state.
 * If the underlying call returns zero, the result is explicitly -1. */
uint16_t gate_rtl_read_resource_byte(uint16_t handle)
{
#ifdef GATE_MSC51_ORIGINAL_FILE_ABI
    /* RTL:e59e..e5b9 allocates one WORD at BP-2 and passes SS:BP-2 to
     * the one-byte DOS read without initializing the other byte.  In the
     * real /Od DOS build, keep the automatic slot exactly as it was; the
     * host oracle instead seeds that unknown stack word explicitly. */
    uint16_t value;
#else
    uint16_t value = gate_rtl_file_stack_word();
#endif
    if (gate_linked_dos_file_read(handle, gate_stack_pointer(&value), 1) != 0)
        return value;
    return GATE_RESOURCE_FILE_BYTE_READ_FAILURE;
}

/* GATE.RTL:0xe5c5..0xe5e2, 0c33:00d4.  Unlike the byte reader above this
 * returns the original stack WORD unconditionally, even if read fails. */
uint16_t gate_rtl_read_resource_word(uint16_t handle)
{
#ifdef GATE_MSC51_ORIGINAL_FILE_ABI
    /* RTL:e5c8..e5dc uses the same uninitialized BP-2 slot, and returns
     * it even if the DOS read does not fill both bytes. */
    uint16_t value;
#else
    uint16_t value = gate_rtl_file_stack_word();
#endif
    (void)gate_linked_dos_file_read(handle, gate_stack_pointer(&value), 2);
    return value;
}

/* GATE.RTL:0xe5e3..0xe5fb, 0c33:00f2. */
uint16_t gate_rtl_read_resource_bytes(uint16_t handle, GateFarPtr destination, uint16_t count)
{
    return gate_linked_dos_file_read(handle, destination, count);
}

/* GATE.RTL:0xe5fc..0xe61d, 0c33:010b.  The original MUL discards the high
 * product word before invoking the byte reader.  The returned AX byte
 * count is then divided by the original element size, not clamped to the
 * requested count.  A zero divisor raises the guest's DIV exception. */
uint16_t gate_rtl_read_resource_elements(uint16_t handle, GateFarPtr destination,
                       uint16_t element_size, uint16_t count)
{
    uint16_t bytes = (uint16_t)(element_size * count);
    uint16_t result = gate_linked_dos_file_read(handle, destination, bytes);
    /* RTL:e617..e61c executes DIV with the original divisor directly.
     * In real mode this naturally raises INT 0 when it is zero.  The
     * host comparator keeps its explicit exception callback instead. */
#ifndef GATE_MSC51_ORIGINAL_FILE_ABI
    if (element_size == 0)
        gate_rtl_file_divide_fault();
#endif
    return (uint16_t)(result / element_size);
}

/* GATE.RTL:0xe61e..0xe636 and 0xe637..0xe64f.  Two distinct original
 * entry points contain identical one-WORD-on-stack write wrappers; both
 * pass a ONE-byte count, so only the low byte is consumed by the callee. */
uint16_t gate_rtl_write_resource_low_byte(uint16_t handle, uint16_t word)
{
    return gate_linked_dos_file_write(handle, gate_stack_pointer(&word), 1);
}

uint16_t gate_rtl_write_resource_low_byte_alternate(uint16_t handle, uint16_t word)
{
    return gate_linked_dos_file_write(handle, gate_stack_pointer(&word), 1);
}

/* GATE.RTL:0xe650..0xe668, 0c33:015f. */
uint16_t gate_rtl_write_resource_bytes(uint16_t handle, GateFarPtr source, uint16_t count)
{
    return gate_linked_dos_file_write(handle, source, count);
}

/* GATE.RTL:0xe669..0xe68a, 0c33:0178.  Matching element-sized write path. */
uint16_t gate_rtl_write_resource_elements(uint16_t handle, GateFarPtr source,
                       uint16_t element_size, uint16_t count)
{
    uint16_t bytes = (uint16_t)(element_size * count);
    uint16_t result = gate_linked_dos_file_write(handle, source, bytes);
    /* RTL:e684..e689 uses the same native 8086 DIV fault semantics. */
#ifndef GATE_MSC51_ORIGINAL_FILE_ABI
    if (element_size == 0)
        gate_rtl_file_divide_fault();
#endif
    return (uint16_t)(result / element_size);
}

/* GATE.RTL:0xe68b..0xe69a, 0c33:019a.  AX from the close call is not used
 * by the original resource-selector caller. */
void gate_rtl_close_resource_file(uint16_t handle)
{
    (void)gate_linked_dos_file_close(handle);
}
