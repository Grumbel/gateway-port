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
constexpr unsigned debug_menu_rows = 6;
// Menu rows: hitboxes, auto-follow, previous RGN, next RGN, dump, close.
int debug_menu_row_at(int x, int y, int width);
void draw_debug_overlay(std::span<std::uint32_t> pixels, int width, int height,
                        const Mouse &mouse, const DebugOverlayData &data,
                        bool menu_open, bool show_hitboxes, unsigned selected_row);
} // namespace gateway
