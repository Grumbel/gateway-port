// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#pragma once
#include "debug_scene.hpp"
#include "mouse.hpp"
#include <cstdint>
#include <span>
#include <string>

namespace gateway {
struct DebugActions {
    bool dump_state = false;
    bool toggle_auto_follow = false;
    int scene_step = 0;
};
struct DebugOverlayData {
    DebugSceneView scene;
    std::string registers;
    std::string object_summary;
    std::string dump_message;
    unsigned guest_ms = 0;
};
struct DebugMenuPosition {
    // Negative coordinates mean the default position: bottom-right.
    int x = -1, y = -1;
};
struct DebugMenuBounds {
    int x, y, width, height;
};
constexpr unsigned debug_menu_rows = 7;
// Menu rows: hitboxes, auto-follow, previous RGN, next RGN, dump, raw candidates, close.
DebugMenuBounds debug_menu_bounds(int screen_width, int screen_height,
                                  DebugMenuPosition position = {});
int debug_menu_row_at(int x, int y, int width, int height,
                      DebugMenuPosition position = {});
bool debug_menu_title_at(int x, int y, int width, int height,
                         DebugMenuPosition position = {});
std::uint32_t debug_hotspot_color(std::uint16_t object_id);
void draw_debug_overlay(std::span<std::uint32_t> pixels, int width, int height,
                        const Mouse &mouse, const DebugOverlayData &data,
                        bool menu_open, bool show_hitboxes, unsigned selected_row,
                        DebugMenuPosition position = {}, bool show_all_regions = false);
} // namespace gateway
