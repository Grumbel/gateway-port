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

void draw_debug_overlay(std::span<std::uint32_t> image, int width, int height,
                        const Mouse &mouse, const DebugOverlayData &data,
                        bool menu_open, bool show_hitboxes, unsigned selected_row,
                        DebugMenuPosition position, bool show_all_regions) {
    if (width < 1 || height < 1 || image.size() < std::size_t(width) * height)
        return;
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
        // group. These fallback rectangles cover the otherwise blank command
        // and text areas and are themselves selectable. They may begin at
        // negative signed coordinates and extend beyond the VGA viewport.
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
