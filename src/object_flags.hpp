// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <optional>

namespace gateway {
// Effective offsets wrap at 16 bits; a word operand reads two contiguous bytes.
using GuestByteReader = std::function<std::uint8_t(std::uint16_t, std::uint32_t)>;
struct GuestWrite {
    std::uint16_t segment, offset;
    std::array<std::uint8_t, 2> bytes{};
    unsigned size = 0;
};
// Proposed live-state writes; invalid object/property/type combinations yield no write.
std::optional<GuestWrite> object_flag_write(const GuestByteReader &read, std::uint16_t ds,
                                            std::uint16_t object, std::uint16_t property,
                                            bool value);
std::uint16_t object_location(const GuestByteReader &read, std::uint16_t ds, std::uint16_t object);
std::optional<GuestWrite> object_location_write(const GuestByteReader &read, std::uint16_t ds,
                                                std::uint16_t object, std::uint16_t location);
// Gateway-specific layout recovered from GATEMAIN.EXE; reads live guest state.
bool object_flag(const GuestByteReader &read, std::uint16_t ds, std::uint16_t object,
                 std::uint16_t property);
} // namespace gateway
