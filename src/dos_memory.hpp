// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace gateway {
struct MemoryResult {
    std::uint16_t error = 0;
    std::uint16_t segment = 0;
    std::uint16_t available = 0;
};
struct MemoryBlock {
    std::uint16_t segment; // First data paragraph; MCB is one paragraph earlier.
    std::uint16_t paragraphs;
    std::uint16_t owner; // PSP segment, or zero for free memory.
};
class DosMemory {
  public:
    // Initially the process owns all conventional memory above its PSP.
    explicit DosMemory(std::uint16_t psp, std::uint16_t end = 0xa000,
                       std::uint16_t environment = 0);
    MemoryResult allocate(std::uint16_t paragraphs);
    MemoryResult release(std::uint16_t segment);
    MemoryResult resize(std::uint16_t segment, std::uint16_t paragraphs);
    const std::vector<MemoryBlock> &blocks() const { return blocks_; }
    void write_mcbs(std::span<std::uint8_t> memory) const;

  private:
    std::uint16_t owner_;
    std::vector<MemoryBlock> blocks_;
    void coalesce();
    void split(std::size_t index, std::uint16_t paragraphs);
};
} // namespace gateway
