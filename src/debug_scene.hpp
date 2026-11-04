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
    // Native rectangle-picker skip bit (0x80), matched against the live
    // guest rectangle table. False means only that this *one* gate passes;
    // it does not establish that an object was painted or selectable.
    std::optional<bool> native_picker_disabled;
};
// Native picker record. The original routine uses signed 16-bit comparisons;
// group fallback rows intentionally start at negative X/Y coordinates.
struct DebugPickerControl {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0; // Signed, inclusive VGA pixels.
    std::uint8_t group = 0, slot = 0, index = 0, kind = 0, flags = 0;
};
struct DebugSceneView {
    std::string picture;
    std::string region_file;
    std::size_t entry = 0, entry_count = 0;
    bool auto_follow = true;
    unsigned x_extent = 2; // Packed rectangles include two screen pixels per X unit.
    std::vector<DebugHitbox> boxes;
    // All picker rows, in original group/slot traversal order. The picker
    // keeps the *last* matching enabled row, including group fallback rows.
    std::vector<DebugPickerControl> native_picker_rows;
    // Non-RGN rows: command/scroll controls and all five selectable fallbacks.
    std::vector<DebugPickerControl> native_controls;
    bool native_picker_aligned = false;
    std::optional<DebugPickerControl> picker_at(int x, int y) const;
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
    // Gateway 1 only. Correlate all rectangles against the original native
    // picker table; an incomplete/mismatched table yields unknown values.
    void set_native_picker_state(std::span<const std::uint8_t> memory, std::uint16_t ds);
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
    std::vector<bool> native_picker_disabled_;
    std::vector<DebugPickerControl> native_picker_rows_;
    std::vector<DebugPickerControl> native_controls_;
    std::string native_picker_picture_;
    std::size_t native_picker_entry_ = 0;
    std::string active_;
    std::size_t entry_ = 0;
    bool auto_follow_ = true;
};
} // namespace gateway
