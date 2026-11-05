// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "debug_ui.hpp"
#include "../third_party/font8x8/font8x8_basic.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string_view>
#include <vector>

namespace gateway {
namespace {
constexpr std::uint32_t panel = 0xff162034;
constexpr std::uint32_t border = 0xff75aaff;
constexpr std::uint32_t white = 0xffe6edfa;
constexpr std::uint32_t muted = 0xffa5b4c5;
constexpr std::uint32_t accent = 0xffffd65c;
constexpr std::uint32_t success = 0xff77efa8;
constexpr std::uint32_t failure = 0xffff7777;
constexpr int menu_width = 354, menu_height = 248;

void pixel(std::span<std::uint32_t> image, int width, int height, int x, int y,
           std::uint32_t color) {
    if (x >= 0 && y >= 0 && x < width && y < height)
        image[std::size_t(y) * width + x] = color;
}
void rect(std::span<std::uint32_t> image, int width, int height, int x0, int y0, int x1, int y1,
          std::uint32_t color, bool filled) {
    x0 = std::clamp(x0, 0, width);
    y0 = std::clamp(y0, 0, height);
    x1 = std::clamp(x1, 0, width);
    y1 = std::clamp(y1, 0, height);
    if (x0 >= x1 || y0 >= y1)
        return;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            if (filled || y == y0 || y == y1 - 1 || x == x0 || x == x1 - 1)
                pixel(image, width, height, x, y, color);
}
void text(std::span<std::uint32_t> image, int width, int height, int x, int y,
          std::string_view message, std::uint32_t color, unsigned limit = 76) {
    for (unsigned i = 0; i < message.size() && i < limit; ++i) {
        const auto c = static_cast<unsigned char>(message[i]);
        if (c >= 128)
            continue;
        for (unsigned row = 0; row < 8; ++row)
            for (unsigned column = 0; column < 8; ++column)
                if (font8x8_basic[c][row] & (1u << column))
                    pixel(image, width, height, x + int(8 * i + column), y + int(row), color);
    }
}
std::uint32_t tint(std::uint32_t base, std::uint32_t color, unsigned alpha) {
    auto channel = [alpha](unsigned old, unsigned next) {
        return (old * (255 - alpha) + next * alpha + 127) / 255;
    };
    return 0xff000000 | (channel((base >> 16) & 255, (color >> 16) & 255) << 16) |
           (channel((base >> 8) & 255, (color >> 8) & 255) << 8) |
           channel(base & 255, color & 255);
}
} // namespace

DebugPickerVerdict debug_picker_verdict(const DebugPickerObservation &observation) {
    if (!observation.model_ready)
        return DebugPickerVerdict::unmodeled;
    const auto &row = observation.predicted;
    const std::array<std::uint16_t, 3> expected{
        std::uint16_t(row ? row->group : 0xffff),
        std::uint16_t(row ? row->slot : 0xffff),
        std::uint16_t(row ? row->kind : 0),
    };
    return observation.native_ax == (row ? 1 : 0) && observation.native_outputs == expected
               ? DebugPickerVerdict::match : DebugPickerVerdict::mismatch;
}

void debug_picker_resolve_source(DebugPickerObservation &observation,
                                 const DebugSceneView &entry_scene) {
    using Source = DebugPickerObservation::Source;
    observation.source = Source::unmodeled;
    observation.native_row.reset();
    observation.native_object_id = 0;
    observation.enabled_overlaps = 0;
    if (!entry_scene.native_picker_decoded)
        return;
    for (const auto &row : entry_scene.native_picker_rows)
        if (!(row.flags & 0x80) && observation.x >= row.x0 && observation.x <= row.x1 &&
            observation.y >= row.y0 && observation.y <= row.y1)
            ++observation.enabled_overlaps;
    if (observation.native_ax == 0) {
        observation.source = Source::none;
        return;
    }
    observation.source = Source::unresolved;
    if (observation.native_ax != 1)
        return;
    for (const auto &row : entry_scene.native_picker_rows) {
        if (row.group != observation.native_outputs[0] ||
            row.slot != observation.native_outputs[1])
            continue;
        // Retain the raw row for a surprising output, but never attribute
        // a skipped, out-of-bounds or wrong-kind row to a visible object.
        observation.native_row = row;
        if (row.kind != observation.native_outputs[2] || (row.flags & 0x80) ||
            observation.x < row.x0 || observation.x > row.x1 ||
            observation.y < row.y0 || observation.y > row.y1)
            return;
        if (row.slot == 0)
            observation.source = Source::fallback;
        else if (!entry_scene.native_picker_aligned)
            observation.source = Source::unmapped;
        else if (entry_scene.native_room_group && row.group == *entry_scene.native_room_group &&
                 row.slot <= entry_scene.boxes.size()) {
            observation.source = Source::room;
            observation.native_object_id = entry_scene.boxes[row.slot - 1].object_id;
        } else
            observation.source = Source::control;
        return;
    }
}

const char *debug_picker_source_name(DebugPickerObservation::Source source) {
    using Source = DebugPickerObservation::Source;
    switch (source) {
    case Source::unmodeled: return "unmodeled";
    case Source::none: return "none";
    case Source::fallback: return "fallback";
    case Source::room: return "room";
    case Source::control: return "control";
    case Source::unmapped: return "unmapped";
    case Source::unresolved: return "unresolved";
    }
    return "unresolved";
}

namespace {
void draw_native_picker_observation(std::span<std::uint32_t> image, int width, int height,
                                    const DebugOverlayData &data) {
    if (!data.native_picker_monitoring)
        return;
    // Stay left of the draggable F10 menu (which defaults to the right) and
    // above the original bottom scrollbar controls. Never replace the
    // winner-map hover legend: that represents a different kind of evidence.
    const int right = std::min(width - 2, 283);
    const int top = std::max(2, height - 119);
    if (right <= 4)
        return;
    rect(image, width, height, 2, top, right, top + 81, panel, true);
    rect(image, width, height, 2, top, right, top + 81, border, false);
    char line[128];
    if (!data.native_picker_error.empty()) {
        text(image, width, height, 8, top + 6, "Native picker capture ERROR", failure, 33);
        text(image, width, height, 8, top + 22, data.native_picker_error, muted, 33);
        return;
    }
    if (!data.native_picker) {
        text(image, width, height, 8, top + 6, "Native picker: awaiting call", muted, 33);
        return;
    }
    const auto &pick = *data.native_picker;
    const auto verdict = debug_picker_verdict(pick);
    const char *label = verdict == DebugPickerVerdict::match ? "MATCH" :
                        verdict == DebugPickerVerdict::mismatch ? "MISMATCH" : "UNMODELED";
    const auto color = verdict == DebugPickerVerdict::match ? success :
                       verdict == DebugPickerVerdict::mismatch ? failure : accent;
    std::snprintf(line, sizeof line, "Native pick @%d,%d t=%ums", pick.x, pick.y, pick.guest_ms);
    text(image, width, height, 8, top + 4, line, white, 33);
    std::snprintf(line, sizeof line, "AX=%u g=%u s=%u k=%u [%s]", unsigned(pick.native_ax),
                  unsigned(pick.native_outputs[0]), unsigned(pick.native_outputs[1]),
                  unsigned(pick.native_outputs[2]), label);
    text(image, width, height, 8, top + 16, line, color, 33);
    const char *source = debug_picker_source_name(pick.source);
    if (pick.native_row && pick.source == DebugPickerObservation::Source::room)
        std::snprintf(line, sizeof line, "%s #%u index%u overlaps%u", source,
                      unsigned(pick.native_object_id), unsigned(pick.native_row->index),
                      pick.enabled_overlaps);
    else if (pick.native_row)
        std::snprintf(line, sizeof line, "%s index%u flags%02x overlaps%u", source,
                      unsigned(pick.native_row->index), unsigned(pick.native_row->flags),
                      pick.enabled_overlaps);
    else
        std::snprintf(line, sizeof line, "%s overlaps%u", source, pick.enabled_overlaps);
    text(image, width, height, 8, top + 28, line, accent, 33);
    if (pick.model_ready) {
        if (pick.predicted)
            std::snprintf(line, sizeof line, "Model g=%u s=%u k=%u", unsigned(pick.predicted->group),
                          unsigned(pick.predicted->slot), unsigned(pick.predicted->kind));
        else
            std::snprintf(line, sizeof line, "Model: no rectangle selected");
        text(image, width, height, 8, top + 40, line, muted, 33);
    } else {
        text(image, width, height, 8, top + 40, "Model: table not decoded", muted, 33);
    }
    if (pick.downstream_candidate) {
        const auto &candidate = *pick.downstream_candidate;
        if (candidate.action_table)
            std::snprintf(line, sizeof line, "Candidate action %02x -> %04x:%04x +%ums",
                          candidate.action, candidate.target_cs, candidate.target_ip,
                          candidate.age_ms);
        else
            std::snprintf(line, sizeof line, "Candidate context %02x obj %x,%x +%ums",
                          candidate.action, candidate.first, candidate.second, candidate.age_ms);
        text(image, width, height, 8, top + 53, line, accent, 33);
    } else if (data.downstream_monitoring) {
        text(image, width, height, 8, top + 53, "Dispatch: no candidate observed", muted, 33);
    } else {
        text(image, width, height, 8, top + 53, "Dispatch: monitoring disabled", muted, 33);
    }
    if (pick.recent_input) {
        const auto &read = *pick.recent_input;
        std::snprintf(line, sizeof line, "Guest 33h/%u n%u @%d,%d +%ums %s",
                      read.function, read.count, read.x, read.y, read.age_ms,
                      read.coordinates_match ? "same" : "diff");
        text(image, width, height, 8, top + 67, line, accent, 33);
    } else if (data.native_input_monitoring) {
        text(image, width, height, 8, top + 67, "Guest mouse: no recent 33h read", muted, 33);
    } else {
        text(image, width, height, 8, top + 67, "Guest mouse: monitoring disabled", muted, 33);
    }
    // Only mark *recent* actual invocations on the map. The current cursor
    // may have moved since the original picker ran; don't label it as native.
    if (data.guest_ms >= pick.guest_ms && data.guest_ms - pick.guest_ms <= 5000 &&
        pick.x >= 0 && pick.x < 640 && pick.y >= 0 && pick.y < height) {
        const int x = int(std::int64_t(pick.x) * width / 640);
        for (int delta = -4; delta <= 4; ++delta) {
            pixel(image, width, height, x + delta, pick.y, color);
            pixel(image, width, height, x, pick.y + delta, color);
        }
    }
}
} // namespace

DebugMenuBounds debug_menu_bounds(int width, int height, DebugMenuPosition position) {
    const int w = std::min(menu_width, std::max(0, width));
    const int h = std::min(menu_height, std::max(0, height));
    const int max_x = std::max(0, width - w), max_y = std::max(0, height - h);
    return {std::clamp(position.x < 0 ? max_x : position.x, 0, max_x),
            std::clamp(position.y < 0 ? max_y : position.y, 0, max_y), w, h};
}

int debug_menu_row_at(int x, int y, int width, int height, DebugMenuPosition position) {
    const auto window = debug_menu_bounds(width, height, position);
    if (x < window.x + 8 || x >= window.x + window.width - 8 ||
        y < window.y + 40 || y >= window.y + 40 + 20 * int(debug_menu_rows))
        return -1;
    return (y - window.y - 40) / 20;
}

bool debug_menu_title_at(int x, int y, int width, int height, DebugMenuPosition position) {
    const auto window = debug_menu_bounds(width, height, position);
    return x >= window.x + 8 && x < window.x + window.width - 8 &&
           y >= window.y + 6 && y < window.y + 34;
}

std::uint32_t debug_hotspot_color(std::uint16_t object_id) {
    // The golden-angle hue step keeps consecutive engine IDs visually distinct.
    const unsigned hue = (unsigned(object_id) * 137 + 20) % 360;
    constexpr unsigned v = 255, s = 195;
    const unsigned p = v * (255 - s) / 255;
    const unsigned f = hue % 60;
    const unsigned q = v * (255 - s * f / 60) / 255;
    const unsigned t = v * (255 - s * (60 - f) / 60) / 255;
    const std::array<std::array<unsigned, 3>, 6> rgb{{
        {{v, t, p}}, {{q, v, p}}, {{p, v, t}},
        {{p, q, v}}, {{t, p, v}}, {{v, p, q}},
    }};
    const auto &c = rgb[hue / 60];
    return 0xff000000 | (c[0] << 16) | (c[1] << 8) | c[2];
}

namespace {
std::uint32_t picker_color(const DebugSceneView &scene, const DebugPickerControl &row) {
    if (row.slot == 0)
        return 0xff648ca0u;
    if (scene.native_room_group && row.group == *scene.native_room_group &&
        row.slot <= scene.boxes.size() && scene.boxes[row.slot - 1].object_id)
        return debug_hotspot_color(scene.boxes[row.slot - 1].object_id);
    return debug_hotspot_color(std::uint16_t(0x800u + row.index));
}

// Rasterize the original picker's actual last-enabled-match rule. Outlining
// every raw rectangle independently gives misleading overlaps; in contrast,
// each pixel here belongs to exactly the group/slot the native selector would
// return for that coordinate. This diagnostic path runs only with F10 active.
void draw_native_picker_winners(std::span<std::uint32_t> image, int width, int height,
                                const Mouse &mouse, const DebugSceneView &scene) {
    const auto &rows = scene.native_picker_rows;
    if (rows.empty() || rows.size() >= 65535)
        return;
    std::vector<std::uint16_t> winner(std::size_t(width) * height);
    std::size_t enabled = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto &row = rows[i];
        if (row.flags & 0x80)
            continue;
        ++enabled;
        const int x0 = std::clamp(int(std::int64_t(row.x0) * width / 640), 0, width);
        const int x1 = std::clamp(int(std::int64_t(row.x1 + 1) * width / 640), 0, width);
        const int y0 = std::clamp(row.y0, 0, height);
        const int y1 = std::clamp(row.y1 + 1, 0, height);
        if (x0 >= x1 || y0 >= y1)
            continue;
        for (int y = y0; y < y1; ++y) {
            auto begin = winner.begin() + std::size_t(y) * width + x0;
            std::fill(begin, begin + (x1 - x0), std::uint16_t(i + 1));
        }
    }
    const auto picked = scene.picker_at(mouse.x, mouse.y);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto offset = std::size_t(y) * width + x;
            const auto id = winner[offset];
            if (!id)
                continue;
            const auto &row = rows[id - 1];
            const bool hover = picked && row.group == picked->group && row.slot == picked->slot;
            const bool outline = x == 0 || x + 1 == width || y == 0 || y + 1 == height ||
                                 winner[offset - 1] != id || winner[offset + 1] != id ||
                                 winner[offset - width] != id || winner[offset + width] != id;
            auto &pixel_value = image[offset];
            const auto color = picker_color(scene, row);
            if (outline)
                pixel_value = hover ? white : color;
            else if (row.slot != 0 || hover)
                pixel_value = tint(pixel_value, color, hover ? 100 : 55);
        }
    }
    // Status panels remain overlays, so render them last and keep them away
    // from the original left-hand scrollbar controls when possible.
    const int status_x = width > 420 ? width - 318 : 2;
    rect(image, width, height, status_x, height - 23, width - 2, height - 2, panel, true);
    char info[128];
    std::snprintf(info, sizeof info, "Native picker %zu/%zu %s", enabled, rows.size(),
                  scene.native_picker_aligned ? "RGN aligned" : "RGN unmapped");
    text(image, width, height, status_x + 6, height - 18, info, white, 37);
    if (picked) {
        if (scene.native_room_group && picked->group == *scene.native_room_group &&
            picked->slot != 0 && picked->slot <= scene.boxes.size()) {
            const auto &box = scene.boxes[picked->slot - 1];
            std::snprintf(info, sizeof info, "Native g%u slot%u Object #%u: %.20s",
                          unsigned(picked->group), unsigned(picked->slot), unsigned(box.object_id),
                          box.name.c_str());
        } else {
            std::snprintf(info, sizeof info, "Native %s g%u slot%u kind%u index%u",
                          scene.native_picker_aligned ? "control" : "unmapped",
                          unsigned(picked->group), unsigned(picked->slot),
                          unsigned(picked->kind), unsigned(picked->index));
        }
        rect(image, width, height, status_x, height - 44, width - 2, height - 24,
             panel, true);
        text(image, width, height, status_x + 6, height - 39, info, accent, 37);
    }
}
} // namespace

void draw_debug_overlay(std::span<std::uint32_t> image, int width, int height,
                        const Mouse &mouse, const DebugOverlayData &data,
                        bool menu_open, bool show_hitboxes, unsigned selected_row,
                        DebugMenuPosition position, bool show_all_regions) {
    if (width < 1 || height < 1 || image.size() < std::size_t(width) * height)
        return;
    const bool show_native_observation = menu_open || show_hitboxes;
    if (show_hitboxes && data.scene.native_picker_decoded && !show_all_regions) {
        draw_native_picker_winners(image, width, height, mouse, data.scene);
        show_hitboxes = false;
    }
    if (show_hitboxes) {
        // The picture occupies columns 260..639 and rows 0..231 in Gateway.
        // When the original picker table has been fully matched, use its
        // verified +261 inclusive bounds, not the approximate RGN origin.
        // Raster ownership merges touching/overlapping shapes belonging to
        // the same engine object, so shared interior edges are never drawn.
        const int left = 260 * width / 640;
        const int native_left = data.scene.native_picker_aligned ? 261 : 260;
        const int native_extent = data.scene.native_picker_aligned ? 1
                                                                    : int(data.scene.x_extent);
        const int area_width = width - left;
        const int area_height = std::min(height, 232);
        std::vector<std::uint32_t> owner(std::size_t(area_width) * area_height);
        constexpr std::uint32_t unnamed_base = 65536;
        std::size_t shown = 0;
        for (std::size_t i = 0; i < data.scene.boxes.size(); ++i) {
            const auto &box = data.scene.boxes[i];
            if (!show_all_regions &&
                (box.native_picker_disabled == true ||
                 (!box.native_picker_disabled &&
                  (box.presence == DebugHitboxPresence::absent ||
                   box.presence == DebugHitboxPresence::undisclosed))))
                continue;
            ++shown;
            const auto id = box.object_id ? std::uint32_t(box.object_id) : unnamed_base + i;
            if (box.polygon.size() >= 3) {
                // Gateway II stores explicit vertices. Scanline filling keeps
                // concave shapes intact without testing every pixel against
                // every polygon edge.
                const int y0 = std::clamp(int(box.y0), 0, area_height);
                const int y1 = std::clamp(int(box.y1 + 1), 0, area_height);
                std::vector<double> crossings;
                crossings.reserve(box.polygon.size());
                for (int y = y0; y < y1; ++y) {
                    crossings.clear();
                    const double scan = y + 0.5;
                    for (std::size_t j = 0; j < box.polygon.size(); ++j) {
                        const auto &a = box.polygon[j];
                        const auto &b = box.polygon[(j + 1) % box.polygon.size()];
                        if ((a.y > scan) == (b.y > scan))
                            continue;
                        const double ratio = (scan - a.y) / (double(b.y) - a.y);
                        const double x = a.x + ratio * (double(b.x) - a.x);
                        crossings.push_back((native_left + x) * width / 640.0 - left);
                    }
                    std::sort(crossings.begin(), crossings.end());
                    for (std::size_t k = 1; k < crossings.size(); k += 2) {
                        const int x0 = std::clamp(int(std::ceil(crossings[k - 1] - 0.5)),
                                                  0, area_width);
                        const int x1 = std::clamp(int(std::ceil(crossings[k] - 0.5)),
                                                  0, area_width);
                        for (int x = x0; x < x1; ++x)
                            owner[std::size_t(y) * area_width + x] = id;
                    }
                }
                continue;
            }
            const int x0 = std::clamp(int((native_left + box.x0) * width / 640), left, width);
            // Packed rectangles cover both pixels in their final X unit;
            // Gateway II's two-vertex boxes use one-pixel units.
            const int x1 = std::clamp(int((native_left + native_extent + box.x1) * width / 640),
                                      left, width);
            const int y0 = std::clamp(int(box.y0), 0, area_height);
            const int y1 = std::clamp(int(box.y1 + 1), 0, area_height);
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x)
                    // Object 0 has no identity: keep such rectangles separate
                    // rather than merging unrelated anonymous regions.
                    owner[std::size_t(y) * area_width + x - left] = id;
        }
        std::uint32_t hovered = 0;
        const int mx = mouse.x * width / 640;
        if (area_width && mx >= left && mx < width && mouse.y >= 0 &&
            mouse.y < area_height)
            hovered = owner[std::size_t(mouse.y) * area_width + mx - left];
        for (int y = 0; y < area_height; ++y) {
            for (int x = 0; x < area_width; ++x) {
                const auto at = std::size_t(y) * area_width + x;
                const auto id = owner[at];
                if (!id)
                    continue;
                const bool outline = x == 0 || x + 1 == area_width || y == 0 ||
                                     y + 1 == area_height || owner[at - 1] != id ||
                                     owner[at + 1] != id || owner[at - area_width] != id ||
                                     owner[at + area_width] != id;
                const auto color = debug_hotspot_color(
                    std::uint16_t(id < unnamed_base ? id : id - unnamed_base));
                auto &pixel_value = image[std::size_t(y) * width + x + left];
                pixel_value = outline ? (id == hovered ? white : color)
                                      : tint(pixel_value, color, id == hovered ? 100 : 55);
            }
        }

        // The native routine scans every row, including slot zero of each
        // group. An unmatched PIC/RGN view still exposes every decoded native
        // row as an anonymous picker rectangle: hiding them would hide real
        // mouse targets whenever the scene tracker lags the original game.
        const auto picked = data.scene.picker_at(mouse.x, mouse.y);
        const DebugPickerControl *hovered_control = nullptr;
        std::size_t active_controls = 0;
        for (const auto &control : data.scene.native_controls) {
            const bool skipped = (control.flags & 0x80) != 0;
            if (skipped && !show_all_regions)
                continue;
            ++active_controls;
            const bool fallback = control.slot == 0;
            const int x0 = std::clamp(int(control.x0 * width / 640), 0, width);
            const int x1 = std::clamp(int((control.x1 + 1) * width / 640), 0, width);
            const int y0 = std::clamp(int(control.y0), 0, height);
            const int y1 = std::clamp(int(control.y1 + 1), 0, height);
            const bool hover = picked
                ? picked->group == control.group && picked->slot == control.slot
                : mouse.x >= control.x0 && mouse.x <= control.x1 &&
                  mouse.y >= control.y0 && mouse.y <= control.y1;
            const auto color = skipped ? 0xff777e89u : fallback ? 0xff648ca0u
                : debug_hotspot_color(std::uint16_t(0x800u + control.index));
            // Keep the large fallback areas transparent unless actually
            // selected; otherwise they would wash out the entire room image.
            if (!fallback || hover)
                for (int y = y0; y < y1; ++y)
                    for (int x = x0; x < x1; ++x) {
                        auto &pixel_value = image[std::size_t(y) * width + x];
                        pixel_value = tint(pixel_value, color, hover ? 75 : 55);
                    }
            rect(image, width, height, x0, y0, x1, y1, hover ? white : color, false);
            if (hover)
                hovered_control = &control;
        }
        // Do not obscure the native scrollbar controls at x=100..260,
        // y=463..479 with the debugger's own status text.
        const int status_x = width > 420 ? width - 318 : 2;
        rect(image, width, height, status_x, height - 23, width - 2, height - 2, panel,
             true);
        char info[128];
        std::snprintf(info, sizeof info, "RGN %s #%zu (%zu/%zu), native %zu",
                      data.scene.region_file.c_str(), data.scene.entry, shown,
                      data.scene.boxes.size(), active_controls);
        text(image, width, height, status_x + 6, height - 18, info, white, 37);
        if (hovered || hovered_control) {
            if (hovered_control) {
                std::snprintf(info, sizeof info,
                              "Native %s g%u slot%u kind%u index%u",
                              !data.scene.native_picker_aligned ? "unmapped" :
                                  hovered_control->slot == 0 ? "fallback" : "control",
                              unsigned(hovered_control->group), unsigned(hovered_control->slot),
                              unsigned(hovered_control->kind), unsigned(hovered_control->index));
            } else if (hovered >= unnamed_base) {
                std::snprintf(info, sizeof info, "Unnamed region #%u",
                              unsigned(hovered - unnamed_base));
            } else {
                const auto id = std::uint16_t(hovered);
                const auto found = std::find_if(data.scene.boxes.begin(), data.scene.boxes.end(),
                                                [id](const auto &box) { return box.object_id == id; });
                const auto name = found != data.scene.boxes.end() && !found->name.empty()
                                      ? found->name.c_str()
                                      : "(name not resolved)";
                const char *gate = found == data.scene.boxes.end() ||
                                           !found->restricted_parser_path
                                       ? ""
                                       : *found->restricted_parser_path ? " [gate:pass]"
                                                                         : " [gate:blocked]";
                std::snprintf(info, sizeof info, "Object #%u: %s%s", id, name, gate);
            }
            rect(image, width, height, status_x, height - 44, width - 2, height - 24,
                 panel, true);
            text(image, width, height, status_x + 6, height - 39, info,
                 hovered_control ? accent :
                     debug_hotspot_color(std::uint16_t(hovered < unnamed_base ? hovered
                                                                            : hovered - unnamed_base)),
                 37);
        }
    }
    if (show_native_observation)
        draw_native_picker_observation(image, width, height, data);
    if (!menu_open)
        return;
    const auto bounds = debug_menu_bounds(width, height, position);
    const int left = bounds.x, top = bounds.y, right = left + bounds.width - 6;
    rect(image, width, height, left, top, right, top + bounds.height, panel, true);
    rect(image, width, height, left, top, right, top + bounds.height, border, false);
    text(image, width, height, left + 13, top + 9, "GATEWAY DEBUG [drag]", accent);
    const std::array<std::string, 7> entries{
        std::string("Hitboxes: ") + (show_hitboxes ? "ON" : "OFF"),
        std::string("Auto scene: ") + (data.scene.auto_follow ? "ON" : "OFF"),
        "Previous RGN frame",
        "Next RGN frame",
        "Dump guest + object state",
        std::string("RGN: ") + (show_all_regions ? "ALL CANDIDATES" : "STATE FILTER"),
        "Close debug menu",
    };
    for (unsigned i = 0; i < entries.size(); ++i) {
        const int y = top + 40 + 20 * int(i);
        if (selected_row == i)
            rect(image, width, height, left + 7, y - 4, right - 7, y + 12, 0xff314a6a,
                 true);
        text(image, width, height, left + 13, y, entries[i],
             selected_row == i ? accent : white, 39);
    }
    char scene[160];
    std::snprintf(scene, sizeof scene, "Scene: %s #%zu %s", data.scene.picture.c_str(),
                  data.scene.entry, data.scene.auto_follow ? "(guess)" : "(manual)");
    text(image, width, height, left + 13, top + 174, scene, white, 39);
    text(image, width, height, left + 13, top + 188, data.registers, muted, 39);
    text(image, width, height, left + 13, top + 202, data.object_summary, muted, 39);
    text(image, width, height, left + 13, top + 216, data.dump_message, accent, 39);
    text(image, width, height, left + 13, top + 233,
         "Up/Dn Left/Right H A R D Esc", muted, 39);
}
} // namespace gateway
