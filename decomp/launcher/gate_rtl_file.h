/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#ifndef GATE_LAUNCHER_RTL_FILE_DECOMP_H
#define GATE_LAUNCHER_RTL_FILE_DECOMP_H

#include "gate_exe.h"

/* Original resource path and DOS file selector operands, RTL:e511..e5c4
 * (linked 0c33:0020..00d4). The type=5 branch requests DOS create by
 * setting bit 7 in the lower-level open flags. The returned all-ones
 * WORD means a byte could not be read, whether because of EOF or error. */
enum {
    GATE_RESOURCE_PATH_PROBE_CODE = 4,
    GATE_RESOURCE_PATH_STACK_BYTES = 80,
    GATE_RESOURCE_PATH_TABLE_ENTRY_SHIFT = 2,
    GATE_RESOURCE_PATH_TABLE_SEGMENT_WORD_OFFSET = 2,
    GATE_RESOURCE_FILE_CREATE_TYPE = 5,
    GATE_DOS_FILE_CREATE_FLAG = 0x80,
    GATE_RESOURCE_FILE_BYTE_READ_FAILURE = 0xffff,
    GATE_DOS_FILE_SEEK_FAILURE = 0xffff,
    /* Original RTL:6640 tests these flags at FAR descriptor byte +0ah.
     * The two early exits do not touch a DOS descriptor or file. */
    GATE_CONTROL_STREAM_FLAGS_OFFSET = 0x0a,
    GATE_CONTROL_STREAM_ACTIVE_MASK = 0x83,
    GATE_CONTROL_STREAM_BYPASS_BIT = 0x40,
    /* RTL:6eb8 stream structure: current FAR pointer at +0, pending
     * count at +4, base FAR pointer at +6, control flags at +0ah. */
    GATE_CONTROL_STREAM_CURRENT_OFFSET = 0,
    GATE_CONTROL_STREAM_CURRENT_SEGMENT = 2,
    GATE_CONTROL_STREAM_PENDING_COUNT = 4,
    GATE_CONTROL_STREAM_BASE_OFFSET = 6,
    GATE_CONTROL_STREAM_BASE_SEGMENT = 8,
    GATE_CONTROL_STREAM_WRITE_MODE = 2,
    GATE_CONTROL_STREAM_MODE_MASK = 3,
    GATE_CONTROL_STREAM_FORCE_WRITE_BIT = 8,
};

/* Source identities are ORIGINAL GATE.RTL file offsets.  These functions
 * were linked into the 0c33 bank in the captured intro, but no runtime
 * segment number is a stable function identity. */
void gate_rtl_set_resource_path_base(GateFarPtr base);
uint16_t gate_rtl_check_resource_path(GateFarPtr path);
uint16_t gate_rtl_open_resource_file(uint16_t type, uint16_t resource);
void gate_rtl_seek_resource_file(uint16_t handle, uint32_t offset);
uint16_t gate_rtl_read_resource_byte(uint16_t handle);
uint16_t gate_rtl_read_resource_word(uint16_t handle);
uint16_t gate_rtl_read_resource_bytes(uint16_t handle, GateFarPtr destination, uint16_t count);
uint16_t gate_rtl_read_resource_elements(uint16_t handle, GateFarPtr destination,
                       uint16_t element_size, uint16_t count);
uint16_t gate_rtl_write_resource_low_byte(uint16_t handle, uint16_t word);
uint16_t gate_rtl_write_resource_low_byte_alternate(uint16_t handle, uint16_t word);
uint16_t gate_rtl_write_resource_bytes(uint16_t handle, GateFarPtr source, uint16_t count);
uint16_t gate_rtl_write_resource_elements(uint16_t handle, GateFarPtr source,
                       uint16_t element_size, uint16_t count);
void gate_rtl_close_resource_file(uint16_t handle);

/* First Ctrl-Break FAR callee RTL:15706. Currently verified for already
 * closed/inactive stream descriptors; active CRT flush/close aborts. */
void gate_linked_shutdown_intro_stream(void);

/* Nested RTL:6eb8..6f45 buffered-flush routine. Implements the branches
 * that do not enter the unresolved CRT descriptor writer. Unsupported
 * writing and relocated descriptor-table branches abort explicitly. */
uint16_t gate_linked_flush_intro_stream(GateFarPtr stream);

/* The original low-level runtime/DOS calls retain their FAR identities.
 * Their complete implementations and ABI have not been recovered yet. */
uint16_t gate_linked_open_runtime_path(GateFarPtr path, uint16_t code);
void gate_linked_format_resource_filename(GateFarPtr output, GateFarPtr format,
                        GateFarPtr base, uint16_t resource, GateFarPtr table);
uint16_t gate_linked_dos_file_open(GateFarPtr filename, uint16_t flags);
uint16_t gate_linked_dos_file_seek(uint16_t handle, uint16_t origin, uint32_t offset);
uint16_t gate_linked_dos_file_read(uint16_t handle, GateFarPtr destination,
                            uint16_t count);
uint16_t gate_linked_dos_file_write(uint16_t handle, GateFarPtr source,
                            uint16_t count);
uint16_t gate_linked_dos_file_close(uint16_t handle);

/* Original routines e59b/e5c5 read an uninitialized stack WORD.  In the
 * guest it holds whatever bytes occupied the local slot on function entry.
 * The host abstraction must supply this entry-state value, not fabricate a
 * success/failure fallback. */
uint16_t gate_rtl_file_stack_word(void);

/* The original 8086 unsigned DIV faults on a zero element size.  This
 * callback must transfer control to the guest's divide-exception handler. */
_Noreturn void gate_rtl_file_divide_fault(void);

#endif
