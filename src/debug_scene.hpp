// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#pragma once
#include "pic.hpp"
#include "regions.hpp"
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace gateway {
struct DebugGameState;

// RGN records describe potential mouse targets, not currently drawn objects.
// The live object's containment chain can reject some targets, but cannot
// prove that a candidate has been painted by the original renderer.
enum class DebugHitboxPresence { unknown, room_candidate, undisclosed, absent };

struct DebugHitbox {
    unsigned x0, y0, x1, y1;
    std::uint16_t object_id;
    std::string name;
    std::vector<RegionPoint> polygon{};
    DebugHitboxPresence presence = DebugHitboxPresence::unknown;
    // Would every enclosing child slot pass the original parser gate *if*
    // its caller requests restricted traversal? Nullopt: no proven answer.
    // It is neither a render-state bit nor proof of mouse pickability.
    std::optional<bool> restricted_parser_path;
};
struct DebugSceneView {
    std::string picture;
    std::string region_file;
    std::size_t entry = 0, entry_count = 0;
    bool auto_follow = true;
    unsigned x_extent = 2; // Packed rectangles include two screen pixels per X unit.
    std::vector<DebugHitbox> boxes;
};

// Best-effort correlation of DOS PIC reads with the independently stored RGN
// rectangles. This is NOT a recovered native "current scene" variable.
class DebugSceneTracker {
  public:
    // names[0] is unused; names[engine_object_id] comes from live guest records.
    explicit DebugSceneTracker(std::vector<std::string> names = {});
    void set_object_names(std::vector<std::string> names) { names_ = std::move(names); }
    // Gateway 1 only. Other Legend games retain unknown presence until their
    // own object-location semantics have been verified.
    void set_game_state(const DebugGameState &state);
    static std::string canonical_file(std::string name);
    static std::string matching_rgn(const std::string &pic);
    bool add_picture(const std::string &file, std::span<const std::uint8_t> pic,
                     std::span<const std::uint8_t> rgn);
    void note_read(const std::string &file, std::size_t offset, std::size_t size);
    void step(int direction); // Cycle through entries with hitboxes; disables automatic follow.
    void auto_follow(bool enabled) { auto_follow_ = enabled; }
    bool auto_follow() const { return auto_follow_; }
    DebugSceneView view() const;

  private:
    struct Picture {
        std::string region_file;
        RegionFormat format = RegionFormat::rectangles;
        std::vector<PicEntry> entries;
        std::vector<RegionFrame> regions;
    };
    std::map<std::string, Picture> pictures_;
    std::vector<std::string> names_;
    std::vector<DebugHitboxPresence> presence_;
    std::vector<std::optional<bool>> restricted_parser_paths_;
    std::string active_;
    std::size_t entry_ = 0;
    bool auto_follow_ = true;
};
} // namespace gateway
