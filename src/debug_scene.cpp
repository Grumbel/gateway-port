// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "debug_scene.hpp"
#include "debug_state.hpp"
#include <algorithm>
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
} // namespace

DebugSceneTracker::DebugSceneTracker(std::vector<std::string> names)
    : names_(std::move(names)) {}

void DebugSceneTracker::set_game_state(const DebugGameState &state) {
    presence_.clear();
    if (state.objects.size() != 734 || !object_at(state, player_id))
        return;
    const auto room = enclosing_room(state, player_id);
    presence_.resize(state.objects.size() + 1, DebugHitboxPresence::unknown);
    if (room)
        for (std::size_t i = 0; i < state.objects.size(); ++i)
            presence_[i + 1] = classify_presence(state, std::uint16_t(i + 1), room);
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
    for (const auto &region : found->second.regions[entry_].boxes) {
        const auto id = region.object_id;
        view.boxes.push_back({region.x0, region.y0, region.x1, region.y1, id,
                              id && id < names_.size() ? names_[id] : "", region.polygon,
                              id && id < presence_.size() ? presence_[id]
                                                           : DebugHitboxPresence::unknown});
    }
    return view;
}
} // namespace gateway
