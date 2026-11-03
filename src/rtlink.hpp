// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gateway {
struct RtlinkFixup {
    std::uint16_t offset, segment;
    std::size_t image_offset;
};
struct RtlinkSection {
    unsigned id;
    std::uint16_t linked_segment, memory_paragraphs, initialized_paragraphs;
    std::uint16_t parent, unknown_word;
    std::string filename;
    std::size_t relocation_file_offset, image_file_offset;
    std::vector<RtlinkFixup> fixups;
    // Exact bytes present on disk. Final paragraph padding may be absent.
    std::vector<std::uint8_t> image;
    std::size_t initialized_size() const { return std::size_t(initialized_paragraphs) * 16; }
    std::size_t memory_size() const { return std::size_t(memory_paragraphs) * 16; }
};
struct RtlinkDirectory {
    std::size_t manager_file_offset;
    std::vector<RtlinkSection> sections;
};
// The supported subset is Gateway's fixed-address, uncompressed external overlays.
// manager_file_offset points to the manager segment in the EXE file, not its MZ image.
RtlinkDirectory parse_rtlink(std::span<const std::uint8_t> exe,
                             std::span<const std::uint8_t> overlay,
                             std::size_t manager_file_offset = 0xa020);
std::vector<std::uint8_t> relocate_rtlink(const RtlinkSection &section, std::uint16_t factor);
// Writes code/fixups and zeroes only the declared BSS. Missing paragraph-tail bytes stay intact.
// Caller owns section residency, parent loads, and any translated-code invalidation.
void apply_rtlink(std::span<std::uint8_t> memory, const RtlinkSection &section,
                  std::uint16_t factor);
} // namespace gateway
