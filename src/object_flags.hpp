#pragma once
#include <cstdint>
#include <functional>

namespace gateway {
// Reads use 16-bit segment offsets, matching the original engine's near arithmetic.
using GuestByteReader = std::function<std::uint8_t(std::uint16_t, std::uint16_t)>;
// Gateway-specific layout recovered from GATEMAIN.EXE; reads live guest state.
bool object_flag(const GuestByteReader &read, std::uint16_t ds, std::uint16_t object,
                 std::uint16_t property);
} // namespace gateway
