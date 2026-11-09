/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* First real ownership boundary for a future source-C DOS main(). It owns
 * the memory and read-only file handles instead of assuming a diagnostic
 * probe's fixed allocation and E: drive layout. The loader's verified data
 * images are deliberately NOT executed: later resident banks, the real
 * callback bindings, IRQ handlers and two linker services are outstanding. */
#include "BOOT.H"
#include <dos.h>

#define GATE_BOOT_DATA_PARAGRAPHS 0x100au
#define GATE_BOOT_RESIDENT_PARAGRAPHS 0x03d0u
#define GATE_BOOT_DATA_SHIFT 0x000au
#define GATE_BOOT_DATA_CRC 0x5a991491ul
#define GATE_BOOT_RESIDENT_CRC 0xf332bf93ul

int gate_dos_prepare_intro_resident_bank_with_exe(uint16_t rtl_handle,
       uint16_t exe_handle, uint16_t resident_segment,
       uint16_t paragraphs, uint16_t data_segment, GateFarPtr exe_callback);
int gate_dos_prepare_intro_data(uint16_t exe_handle, uint16_t rtl_handle,
       uint16_t lower_segment, uint16_t paragraphs,
       uint16_t resident_segment, uint16_t external_a_segment,
       uint16_t external_b_segment, GateFarPtr return_callback);
int gate_dos_prepare_intro_data_source(uint16_t exe_handle, uint16_t rtl_handle,
       uint16_t lower_segment, uint16_t paragraphs,
       uint16_t resident_segment, uint16_t external_a_segment,
       uint16_t external_b_segment, GateFarPtr return_callback);
uint32_t gate_dos_intro_resident_crc32_with_callback(uint16_t resident_segment,
                                       uint16_t paragraphs,
                                       uint16_t expected_callback_offset);
uint32_t gate_dos_intro_data_crc32(uint16_t lower_segment,
                                   uint16_t paragraphs);

void gate_dos_boot_release(GateDosBootImage *image)
{
    if (image == 0)
        return;
    /* Do not leave a dangling guest DS if the caller releases the image. */
    gate_dos_bind_ds(0u);
    if (image->resident_segment != 0u) {
        _dos_freemem(image->resident_segment);
        image->resident_segment = 0u;
    }
    if (image->lower_segment != 0u) {
        _dos_freemem(image->lower_segment);
        image->lower_segment = 0u;
    }
}

int gate_dos_boot_prepare(const GateDosBootConfig *config,
                          GateDosBootImage *image)
{
    unsigned int allocated = 0u;
    GateDosFileResult result;
    uint16_t exe_handle = 0u, rtl_handle = 0u;
    int exe_open = 0, rtl_open = 0;
    int error = 0;
    int data_status;

    if (image == 0 || image->lower_segment != 0u ||
        image->resident_segment != 0u)
        return -1;
    if (config == 0 || config->exe_path.segment == 0u ||
        config->rtl_path.segment == 0u ||
        config->exe_callback.segment == 0u ||
        config->return_callback.segment == 0u ||
        config->external_a_segment == 0u ||
        config->external_b_segment == 0u || config->source_c_return > 1u)
        return -1;

    if (_dos_allocmem(GATE_BOOT_DATA_PARAGRAPHS, &allocated) != 0u)
        return -2;
    if (allocated == 0u || allocated > 0xfff5u) {
        if (allocated != 0u)
            _dos_freemem(allocated);
        return -2;
    }
    image->lower_segment = (uint16_t)allocated;
    if (_dos_allocmem(GATE_BOOT_RESIDENT_PARAGRAPHS, &allocated) != 0u) {
        error = -3;
        goto fail;
    }
    if (allocated == 0u || allocated > 0xfc30u) {
        if (allocated != 0u)
            _dos_freemem(allocated);
        error = -3;
        goto fail;
    }
    image->resident_segment = (uint16_t)allocated;

    result = gate_dos_file_open(config->exe_path, 0u, 0u);
    if (result.carry != 0u) {
        error = -4;
        goto fail;
    }
    exe_open = 1;
    exe_handle = result.ax;
    result = gate_dos_file_open(config->rtl_path, 0u, 0u);
    if (result.carry != 0u) {
        error = -5;
        goto fail;
    }
    rtl_open = 1;
    rtl_handle = result.ax;

    if (gate_dos_prepare_intro_resident_bank_with_exe(rtl_handle, exe_handle,
            image->resident_segment, GATE_BOOT_RESIDENT_PARAGRAPHS,
            (uint16_t)(image->lower_segment + GATE_BOOT_DATA_SHIFT),
            config->exe_callback) != 0) {
        error = -6;
        goto fail;
    }
    if (gate_dos_intro_resident_crc32_with_callback(image->resident_segment,
            GATE_BOOT_RESIDENT_PARAGRAPHS, config->exe_callback.offset) !=
            GATE_BOOT_RESIDENT_CRC) {
        error = -9;
        goto fail;
    }
    if (config->source_c_return == 1u)
        data_status = gate_dos_prepare_intro_data_source(exe_handle, rtl_handle,
            image->lower_segment, GATE_BOOT_DATA_PARAGRAPHS,
            image->resident_segment, config->external_a_segment,
            config->external_b_segment, config->return_callback);
    else
        data_status = gate_dos_prepare_intro_data(exe_handle, rtl_handle,
            image->lower_segment, GATE_BOOT_DATA_PARAGRAPHS,
            image->resident_segment, config->external_a_segment,
            config->external_b_segment, config->return_callback);
    if (data_status != 0) {
        /* The data compositor has eleven distinct failure points.
         * Surface its exact -1..-11 code as -71..-81 rather than the
         * ambiguous -7, without changing cleanup or CRC handling. */
        error = -70 + data_status;
        goto fail;
    }
    if (gate_dos_intro_data_crc32(image->lower_segment,
                                  GATE_BOOT_DATA_PARAGRAPHS) !=
                                  GATE_BOOT_DATA_CRC) {
        error = -10;
        goto fail;
    }

fail:
    /* Always attempt both closes even if one fails; release memory only
     * after the last file operation, retaining the first failure cause. */
    if (rtl_open) {
        result = gate_dos_file_close(rtl_handle);
        if (result.carry != 0u && error == 0)
            error = -8;
    }
    if (exe_open) {
        result = gate_dos_file_close(exe_handle);
        if (result.carry != 0u && error == 0)
            error = -8;
    }
    if (error != 0)
        gate_dos_boot_release(image);
    return error;
}
