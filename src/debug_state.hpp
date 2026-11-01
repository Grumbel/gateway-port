#pragma once
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

// Recognize the known Gateway 1 live object table, or return nullopt when DS
// belongs to another program/temporary segment. No guest memory is modified.
std::optional<DebugGameState> inspect_gateway_state(std::span<const std::uint8_t> memory,
                                                     std::uint16_t ds);
std::string debug_objects_tsv(const DebugGameState &state,
                              std::span<const std::uint8_t> object_dat = {});
} // namespace gateway
