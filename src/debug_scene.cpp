#include "debug_scene.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <stdexcept>

namespace gateway {
DebugSceneTracker::DebugSceneTracker(std::vector<std::string> names)
    : names_(std::move(names)) {}

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
    if (file.size() != 12 || file.substr(0, 5) != "GATE_" ||
        file.substr(8) != ".PIC" || !std::isdigit(file[5]) || !std::isdigit(file[6]) ||
        !std::isdigit(file[7]))
        return {};
    const int number = (file[5] - '0') * 100 + (file[6] - '0') * 10 + file[7] - '0';
    const int candidate = number >= 100 && number < 200 ? number - 100 : number;
    char name[16];
    std::snprintf(name, sizeof name, "GATE_%03d.RGN", candidate);
    return name;
}

bool DebugSceneTracker::add_picture(const std::string &name, std::span<const std::uint8_t> pic,
                                     std::span<const std::uint8_t> rgn) {
    const auto file = canonical_file(name);
    const auto region_file = matching_rgn(file);
    if (region_file.empty())
        return false;
    auto entries = parse_pic_directory(pic);
    auto regions = parse_regions(rgn, entries.size());
    pictures_.insert_or_assign(file, Picture{region_file, std::move(entries), std::move(regions)});
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
    view.entry = entry_;
    view.entry_count = found->second.regions.size();
    for (const auto &region : found->second.regions[entry_].boxes) {
        const auto id = region.object_id;
        view.boxes.push_back({region.x0, region.y0, region.x1, region.y1, id,
                              id && id < names_.size() ? names_[id] : ""});
    }
    return view;
}
} // namespace gateway
