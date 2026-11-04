// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "debug_scene.hpp"
#include "debug_state.hpp"
#include "story_model.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <stdexcept>

namespace gateway {
namespace {
constexpr std::uint16_t player_id = 0x0d3;
constexpr std::uint16_t drawer_id = 0x124;

const DebugObjectState *object_at(const DebugGameState &state, std::uint16_t id) {
    if (!id || id > state.objects.size() || state.objects[id - 1].id != id)
        return nullptr;
    return &state.objects[id - 1];
}

// Return only a confirmed type-1 enclosing room; a broken, cyclic or
// unsupported location chain is not evidence of absence.
std::uint16_t enclosing_room(const DebugGameState &state, std::uint16_t id) {
    for (std::size_t depth = 0; depth <= state.objects.size(); ++depth) {
        const auto *object = object_at(state, id);
        if (!object)
            return 0;
        if (object->type == 1)
            return id;
        if (!object->location || !*object->location)
            return 0;
        id = *object->location;
    }
    return 0;
}

DebugHitboxPresence classify_presence(const DebugGameState &state, std::uint16_t id,
                                      std::uint16_t current_room) {
    const auto *object = object_at(state, id);
    if (!object || !current_room)
        return DebugHitboxPresence::unknown;
    if (object->type == 1)
        return id == current_room ? DebugHitboxPresence::room_candidate
                                  : DebugHitboxPresence::absent;
    const bool undisclosed = object->disclosed == false;
    // Type-6 links can describe a passage between *two* rooms. Its direct
    // location is only the first endpoint; do not hide the passage from the
    // second endpoint merely because it is not the direct location.
    if (object->type == 6) {
        if (object->raw.size() < 0x0e)
            return DebugHitboxPresence::unknown;
        const auto second = std::uint16_t(object->raw[0x0c] | unsigned(object->raw[0x0d]) << 8);
        if (second == current_room)
            return undisclosed ? DebugHitboxPresence::undisclosed
                               : DebugHitboxPresence::room_candidate;
        if (!second && object->location && *object->location != current_room)
            return DebugHitboxPresence::unknown;
    }
    for (std::size_t depth = 0; depth <= state.objects.size(); ++depth) {
        if (!object->location)
            return DebugHitboxPresence::unknown;
        const auto parent_id = *object->location;
        if (!parent_id || parent_id == player_id)
            return DebugHitboxPresence::absent;
        const auto *parent = object_at(state, parent_id);
        if (!parent)
            return DebugHitboxPresence::unknown;
        if (parent->type == 1)
            return parent_id != current_room ? DebugHitboxPresence::absent
                   : undisclosed ? DebugHitboxPresence::undisclosed
                                 : DebugHitboxPresence::room_candidate;
        // Verified first-room property: the book can remain in the drawer's
        // containment chain while the drawer is closed. This is only a
        // conservative exclusion, not a generic rendering/visibility rule.
        if (parent_id == drawer_id && state.drawer_open == false)
            return DebugHitboxPresence::absent;
        object = parent;
    }
    return DebugHitboxPresence::unknown;
}

// This *does not* classify the renderer or the actual mouse target. The
// resident parser's 0385:0a9a gate only runs when its caller requests a
// restricted traversal. Walk the known parent chain and ask whether every
// indexed child slot would pass that particular gate in the live state.
std::optional<bool> restricted_parser_path(const DebugGameState &state,
                                           const StoryModel &model, std::uint16_t id,
                                           std::uint16_t current_room) {
    if (!current_room || enclosing_room(state, id) != current_room)
        return {};
    for (std::size_t depth = 0; depth <= state.objects.size(); ++depth) {
        const auto *child = object_at(state, id);
        if (!child || !child->location || !*child->location)
            return {};
        const auto *parent = object_at(state, *child->location);
        if (!parent || parent->type < 1 || parent->type > 8)
            return {};
        if (parent->type != 1 && parent->type != 7) {
            // Only types 2, 7 and 8 have verified child-slot byte accessors.
            const std::size_t slot_offset = child->type == 2 ? 0x10 : 0x0e;
            if (child->type != 2 && child->type != 7 && child->type != 8)
                return {};
            if (child->raw.size() <= slot_offset || !parent->parser_gate_flags)
                return {};
            if (!model.can_enumerate_child_slot(parent->id, std::uint8_t(parent->type),
                                                 child->raw[slot_offset]))
                return false;
        }
        if (parent->type == 1)
            return parent->id == current_room;
        id = parent->id;
    }
    return {};
}
} // namespace

DebugSceneTracker::DebugSceneTracker(std::vector<std::string> names)
    : names_(std::move(names)) {}

void DebugSceneTracker::set_game_state(const DebugGameState &state) {
    presence_.clear();
    restricted_parser_paths_.clear();
    if (state.objects.size() != 734 || !object_at(state, player_id))
        return;
    const auto room = enclosing_room(state, player_id);
    presence_.resize(state.objects.size() + 1, DebugHitboxPresence::unknown);
    restricted_parser_paths_.resize(state.objects.size() + 1);
    if (room) {
        StoryModel parser_model;
        constexpr std::array<std::uint16_t, 4> properties{0x0c, 0x13, 0x1f, 0x1c};
        for (std::size_t i = 0; i < state.objects.size(); ++i) {
            const auto &object = state.objects[i];
            const auto id = std::uint16_t(i + 1);
            parser_model.define(id);
            if (!object.parser_gate_flags)
                continue;
            for (unsigned bit = 0; bit < properties.size(); ++bit)
                if (*object.parser_gate_flags & (1u << bit))
                    parser_model.initial_flag(id, properties[bit]);
        }
        for (std::size_t i = 0; i < state.objects.size(); ++i) {
            presence_[i + 1] = classify_presence(state, std::uint16_t(i + 1), room);
            restricted_parser_paths_[i + 1] =
                restricted_parser_path(state, parser_model, std::uint16_t(i + 1), room);
        }
    }
}

void DebugSceneTracker::set_native_picker_state(std::span<const std::uint8_t> memory,
                                                std::uint16_t ds) {
    native_picker_disabled_.clear();
    native_controls_.clear();
    native_picker_picture_ = active_;
    native_picker_entry_ = entry_;
    const auto picture = pictures_.find(active_);
    if (picture == pictures_.end() || !active_.starts_with("GATE_") ||
        picture->second.format != RegionFormat::rectangles ||
        entry_ >= picture->second.regions.size())
        return;
    const auto &boxes = picture->second.regions[entry_].boxes;
    if (boxes.empty() || boxes.size() >= 35)
        return;

    // Native Gateway 1 picker, GATE.RTL:0x14186, observed at 2649:0541:
    // groups are 35-entry index lists at DS:a974; the indexed rects have
    // two-byte x/y bounds in DS:a6d0..a8cf and a skip bit in DS:a920.
    // Match the *entire* original room group by geometry and result code 3
    // before interpreting any flags. Other games/layouts remain unknown.
    auto byte_at = [&](std::uint16_t seg, std::size_t offset) -> std::optional<std::uint8_t> {
        const auto physical = std::size_t(seg) * 16 + offset;
        if (physical >= memory.size())
            return {};
        return memory[physical];
    };
    auto word_at = [&](std::uint16_t seg, std::size_t offset) -> std::optional<std::uint16_t> {
        const auto lo = byte_at(seg, offset), hi = byte_at(seg, offset + 1);
        if (!lo || !hi)
            return {};
        return std::uint16_t(*lo | unsigned(*hi) << 8);
    };
    const auto index_seg = word_at(ds, 0x8cc6);
    if (!index_seg)
        return;
    const auto group_count = word_at(*index_seg, 0x1576);
    if (!group_count || !*group_count || *group_count > 6)
        return;

    struct NativeRect {
        std::uint8_t index = 0, kind = 0, flags = 0;
        std::uint16_t x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    };
    std::vector<std::vector<NativeRect>> groups;
    groups.reserve(*group_count);
    // Decode the *entire* live picker table. It contains UI control groups in
    // addition to the picture-region group; PIC/RGN files describe only the
    // latter. Do not expose any control unless every indexed row is valid.
    for (unsigned group = 0; group < *group_count; ++group) {
        const auto count = byte_at(ds, 0xa8d4 + group);
        if (!count || !*count || *count > 35)
            return;
        std::vector<NativeRect> rows;
        rows.reserve(*count);
        for (unsigned i = 0; i < *count; ++i) {
            const auto index = byte_at(ds, 0xa974 + group * 35 + i);
            if (!index || *index >= 64)
                return;
            const auto x0 = word_at(ds, 0xa6d0 + *index * 2);
            const auto x1 = word_at(ds, 0xa750 + *index * 2);
            const auto y0 = word_at(ds, 0xa7d0 + *index * 2);
            const auto y1 = word_at(ds, 0xa850 + *index * 2);
            const auto flags = byte_at(ds, 0xa920 + *index);
            const auto kind = byte_at(ds, 0xaa4e + *index);
            if (!x0 || !x1 || !y0 || !y1 || !flags || !kind ||
                *kind < 1 || *kind > 6 ||
                (i != 0 && *kind != 1 &&
                 (*x0 > *x1 || *y0 > *y1 || *x1 >= 640 || *y1 >= 480)))
                return;
            rows.push_back({*index, *kind, *flags, *x0, *x1, *y0, *y1});
        }
        groups.push_back(std::move(rows));
    }

    std::optional<std::size_t> matching_group;
    for (std::size_t group = 0; group < groups.size(); ++group) {
        const auto &rows = groups[group];
        if (rows.size() != boxes.size() + 1 || rows.front().kind != 1)
            continue;
        std::vector<bool> disabled;
        disabled.reserve(boxes.size());
        for (std::size_t i = 0; i < boxes.size(); ++i) {
            const auto &row = rows[i + 1];
            const auto &box = boxes[i];
            if (row.kind != 3 || row.x0 != box.x0 + 261 ||
                row.x1 != box.x1 + 261 || row.y0 != box.y0 ||
                row.y1 != box.y1)
                break;
            disabled.push_back((row.flags & 0x80) != 0);
        }
        if (disabled.size() == boxes.size()) {
            // A second matching group would be ambiguous. Do not guess.
            if (matching_group) {
                native_picker_disabled_.clear();
                return;
            }
            matching_group = group;
            native_picker_disabled_ = std::move(disabled);
        }
    }
    if (!matching_group)
        return;
    for (std::size_t group = 0; group < groups.size(); ++group) {
        if (group == *matching_group)
            continue;
        // Group headers (kind 1) define clipping/bounds; they are not
        // selectable rectangles. Retain only actual non-RGN picker entries.
        for (std::size_t i = 1; i < groups[group].size(); ++i) {
            const auto &row = groups[group][i];
            if (row.kind != 1)
                native_controls_.push_back({row.x0, row.y0, row.x1, row.y1,
                                            std::uint8_t(group), row.index,
                                            row.kind, row.flags});
        }
    }
}

std::string DebugSceneTracker::canonical_file(std::string file) {
    const auto slash = file.find_last_of("\\/:");
    if (slash != std::string::npos)
        file.erase(0, slash + 1);
    std::transform(file.begin(), file.end(), file.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return file;
}

std::string DebugSceneTracker::matching_rgn(const std::string &picture) {
    const auto file = canonical_file(picture);
    if (file.size() == 12 && file.substr(8) == ".PIC" &&
        (file.substr(0, 5) == "GATE_" || file.substr(0, 5) == "HOME_" ||
         file.substr(0, 5) == "S301_") &&
        std::isdigit(static_cast<unsigned char>(file[5])) &&
        std::isdigit(static_cast<unsigned char>(file[6])) &&
        std::isdigit(static_cast<unsigned char>(file[7]))) {
        const int number = (file[5] - '0') * 100 + (file[6] - '0') * 10 + file[7] - '0';
        const int candidate = number >= 100 && number < 200 ? number - 100 : number;
        char name[16];
        std::snprintf(name, sizeof name, "%s%03d.RGN", file.substr(0, 5).c_str(), candidate);
        return name;
    }
    if (file.size() == 11 && file[0] == 'S' &&
        (file.substr(0, 4) == "S101" || file.substr(0, 4) == "S201") &&
        file[4] == '_' && (file[5] == 'E' || file[5] == 'C') &&
        std::isdigit(static_cast<unsigned char>(file[6])) && file.substr(7) == ".PIC")
        return file.substr(0, 5) + 'R' + file[6] + ".RGN";
    return {};
}

bool DebugSceneTracker::add_picture(const std::string &name, std::span<const std::uint8_t> pic,
                                     std::span<const std::uint8_t> rgn) {
    const auto file = canonical_file(name);
    const auto region_file = matching_rgn(file);
    if (region_file.empty())
        return false;
    auto entries = parse_pic_directory(pic);
    const auto format = file.starts_with("HOME_") ? RegionFormat::polygons
                        : (file.starts_with("S101_") || file.starts_with("S201_"))
                            ? RegionFormat::early_rectangles
                            : RegionFormat::rectangles;
    auto regions = parse_regions(rgn, entries.size(), format);
    pictures_.insert_or_assign(file,
                               Picture{region_file, format, std::move(entries), std::move(regions)});
    return true;
}

void DebugSceneTracker::note_read(const std::string &file, std::size_t offset, std::size_t size) {
    if (!auto_follow_ || !size)
        return;
    const auto found = pictures_.find(canonical_file(file));
    if (found == pictures_.end())
        return;
    const auto &entries = found->second.entries;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto &entry = entries[i];
        if (!entry.offset || !entry.width || !entry.height || (entry.flags & 0x4000) ||
            entry.width < 260 || entry.height < 150 || offset < entry.offset)
            continue;
        // The index table is ordered in Gateway, but use the next larger offset
        // rather than assuming ordering for all Legend dialects.
        std::size_t end = static_cast<std::size_t>(-1);
        for (const auto &next : entries)
            if (next.offset > entry.offset)
                end = std::min(end, std::size_t(next.offset));
        if (offset >= end)
            continue;
        active_ = found->first;
        entry_ = i;
        return;
    }
}

void DebugSceneTracker::step(int direction) {
    if (active_.empty() || !direction)
        return;
    const auto &frames = pictures_.at(active_).regions;
    if (frames.empty())
        return;
    auto_follow_ = false;
    for (std::size_t n = 0; n < frames.size(); ++n) {
        entry_ = direction > 0 ? (entry_ + 1) % frames.size()
                               : (entry_ + frames.size() - 1) % frames.size();
        if (!frames[entry_].boxes.empty())
            return;
    }
}

DebugSceneView DebugSceneTracker::view() const {
    DebugSceneView view;
    view.auto_follow = auto_follow_;
    const auto found = pictures_.find(active_);
    if (found == pictures_.end())
        return view;
    view.picture = found->first;
    view.region_file = found->second.region_file;
    view.x_extent = found->second.format == RegionFormat::polygons ? 1 : 2;
    view.entry = entry_;
    view.entry_count = found->second.regions.size();
    const bool picker_matches = native_picker_picture_ == active_ &&
                                native_picker_entry_ == entry_ &&
                                native_picker_disabled_.size() ==
                                    found->second.regions[entry_].boxes.size();
    view.native_picker_aligned = picker_matches;
    if (picker_matches)
        view.native_controls = native_controls_;
    std::size_t index = 0;
    for (const auto &region : found->second.regions[entry_].boxes) {
        const auto id = region.object_id;
        view.boxes.push_back({region.x0, region.y0, region.x1, region.y1, id,
                              id && id < names_.size() ? names_[id] : "", region.polygon,
                              id && id < presence_.size() ? presence_[id]
                                                           : DebugHitboxPresence::unknown,
                              id && id < restricted_parser_paths_.size()
                                  ? restricted_parser_paths_[id] : std::nullopt,
                              picker_matches ? std::optional<bool>(native_picker_disabled_[index])
                                             : std::nullopt});
        ++index;
    }
    return view;
}
} // namespace gateway
