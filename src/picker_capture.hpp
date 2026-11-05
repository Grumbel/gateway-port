// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace gateway {

// The register values are captured BEFORE executing the original picker.
// The corresponding .bin is private guest RAM, never a distributable fixture.
struct PickerEntryCapture {
    unsigned call = 0, guest_ms = 0;
    int x = 0, y = 0;
    std::uint16_t return_cs = 0, return_ip = 0;
    std::string picture;
    std::size_t picture_entry = 0;
    // AX BX CX DX CS IP SS SP DS ES BP FLAGS, in this exact order.
    std::array<std::uint16_t, 12> registers{};
};

// Writes call-NNNN.bin (one MiB) and call-NNNN.meta, refusing to overwrite.
// The returned basename identifies both files. Callers must create an empty
// capture directory and independently report any failure to the user.
std::string write_picker_entry_capture(const std::filesystem::path &directory,
                                       const PickerEntryCapture &capture,
                                       std::span<const std::uint8_t> guest_ram);

} // namespace gateway
