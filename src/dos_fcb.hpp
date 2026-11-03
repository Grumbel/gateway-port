// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#pragma once
#include <array>
#include <cstdint>
#include <string_view>
namespace gateway {
struct ParsedFcb {
    std::array<std::uint8_t, 16> bytes{};
    std::size_t consumed = 0;
    std::uint8_t status = 0; // 0 = plain, 1 = wildcard, ff = unavailable drive.
};
ParsedFcb parse_fcb(std::string_view input, std::uint8_t flags,
                    std::array<std::uint8_t, 16> previous);
} // namespace gateway
