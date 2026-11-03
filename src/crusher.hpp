// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gateway {
// Crusher! 3.0, as used by Mission Critical's PIC image records. The caller supplies the
// uncompressed byte count (width * height); the compressed stream has no size header for it.
std::vector<std::uint8_t> decode_crusher(std::span<const std::uint8_t> stream,
                                         std::size_t uncompressed_size);
} // namespace gateway
