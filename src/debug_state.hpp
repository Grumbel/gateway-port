// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gateway {
struct DebugObjectState {
    std::uint16_t id, type, segment, offset, handler, name_token, secondary_name_token;
    std::optional<std::uint16_t> location;
    std::vector<std::uint8_t> raw;
};
struct DebugGameState {
    std::uint16_t ds = 0;
    std::vector<DebugObjectState> objects;
    std::optional<std::uint16_t> book_location;
    std::optional<bool> drawer_open;
};

struct DebugNameTable {
    std::size_t directory_address = 0;
    std::vector<std::string> names; // Engine ID 0 is unused.
};

// Recognize the known Gateway 1 live object table, or return nullopt when DS
// belongs to another program/temporary segment. No guest memory is modified.
std::optional<DebugGameState> inspect_gateway_state(std::span<const std::uint8_t> memory,
                                                     std::uint16_t ds);
std::string debug_objects_tsv(const DebugGameState &state,
                              std::span<const std::uint8_t> object_dat = {});
// Use the engine object IDs as keys (index 0 is unused). RGN records use
// these IDs; OBJECT.DAT string indexes are not interchangeable with them.
std::vector<std::string> debug_engine_object_names(
    const DebugGameState &state, std::span<const std::uint8_t> object_dat);

// Other Legend versions can relocate their native object directory. Look for
// a fully validated sequence of six-byte directory entries whose record-name
// tokens point into the game's own OBJECT.DAT string pool. Never use string
// enumeration positions as engine IDs. An unrecognized layout returns nullopt.
std::optional<DebugNameTable> inspect_legend_name_table(
    std::span<const std::uint8_t> memory, std::span<const std::uint8_t> object_dat,
    unsigned required_id = 0, std::optional<std::size_t> directory_hint = {});
} // namespace gateway
