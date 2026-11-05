// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "debug_ui.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
} // namespace
int main() try {
    const auto bounds = gateway::debug_menu_bounds(640, 480);
    require(bounds.x == 286 && bounds.y == 232, "menu defaults below Gateway scene");
    require(gateway::debug_menu_row_at(500, 272, 640, 480) == 0 &&
                gateway::debug_menu_row_at(500, 372, 640, 480) == 5 &&
                gateway::debug_menu_row_at(500, 392, 640, 480) == 6 &&
                gateway::debug_menu_row_at(500, 48, 640, 480) < 0 &&
                gateway::debug_menu_title_at(500, 245, 640, 480),
            "bottom-right debug menu hit testing");
    const gateway::DebugMenuPosition moved{40, 50};
    require(gateway::debug_menu_row_at(100, 90, 640, 480, moved) == 0 &&
                gateway::debug_menu_title_at(100, 62, 640, 480, moved) &&
                !gateway::debug_menu_title_at(100, 120, 640, 480, moved),
            "relocated menu mouse interaction");
    require(gateway::debug_menu_bounds(640, 480, {999, 999}).x == 286 &&
                gateway::debug_menu_bounds(640, 480, {999, 999}).y == 232,
            "dragging cannot move window out of bounds");
    std::vector<std::uint32_t> pixels(640 * 480, 0xff123456);
    gateway::Mouse mouse;
    mouse.x = 265;
    mouse.y = 15;
    gateway::DebugOverlayData debug;
    debug.scene.region_file = "GATE_002.RGN";
    debug.scene.entry = 15;
    debug.scene.boxes.push_back({0, 10, 10, 20, 292, "drawer", {}, {}, {}});
    debug.scene.boxes.push_back({12, 10, 20, 20, 292, "drawer", {}, {}, {}});
    debug.scene.boxes.push_back({30, 10, 36, 20, 222, "wall", {}, {}, {}});
    debug.scene.boxes.push_back({40, 10, 50, 20, 0, "", {}, {}, {}});
    debug.scene.boxes.push_back({52, 10, 62, 20, 0, "", {}, {}, {}});
    gateway::draw_debug_overlay(pixels, 640, 480, mouse, debug, false, true, 0);
    require(pixels[10 * 640 + 260] == 0xffe6edfa,
            "hovered RGN object contour not drawn at scene x=260");
    require(pixels[15 * 640 + 271] != 0xffe6edfa &&
                pixels[15 * 640 + 271] != 0xff123456 &&
                pixels[15 * 640 + 271] == pixels[15 * 640 + 272],
            "adjacent rectangles of one object must have a translucent merged interior");
    require(pixels[15 * 640 + 290] == gateway::debug_hotspot_color(222) &&
                gateway::debug_hotspot_color(222) != gateway::debug_hotspot_color(292),
            "distinct engine objects should have distinct outline colors");
    require(pixels[15 * 640 + 311] == gateway::debug_hotspot_color(3) &&
                pixels[15 * 640 + 312] == gateway::debug_hotspot_color(4),
            "anonymous adjacent rectangles must not merge into one object");
    require(pixels[10 * 640 + 0] == 0xff123456,
            "debug overlay moved picture data on the left side");
    debug.scene.boxes[2].presence = gateway::DebugHitboxPresence::absent;
    std::vector<std::uint32_t> filtered(640 * 480, 0xff123456);
    gateway::draw_debug_overlay(filtered, 640, 480, mouse, debug, false, true, 0);
    require(filtered[15 * 640 + 290] == 0xff123456,
            "default outline excludes a guest-confirmed off-room target");
    debug.scene.boxes[2].presence = gateway::DebugHitboxPresence::undisclosed;
    gateway::draw_debug_overlay(filtered, 640, 480, mouse, debug, false, true, 0);
    require(filtered[15 * 640 + 290] == 0xff123456,
            "default outline excludes a not-yet-disclosed room candidate");
    gateway::draw_debug_overlay(filtered, 640, 480, mouse, debug, false, true, 0, {}, true);
    require(filtered[15 * 640 + 290] == gateway::debug_hotspot_color(222),
            "raw debugger still exposes every candidate RGN target");
    debug.scene.boxes[2].presence = gateway::DebugHitboxPresence::room_candidate;
    debug.scene.boxes[2].native_picker_disabled = true;
    std::vector<std::uint32_t> disabled(640 * 480, 0xff123456);
    gateway::draw_debug_overlay(disabled, 640, 480, mouse, debug, false, true, 0);
    require(disabled[15 * 640 + 290] == 0xff123456,
            "native picker 0x80 skip flag hides a single inactive RGN rectangle");
    gateway::draw_debug_overlay(disabled, 640, 480, mouse, debug, false, true, 0, {}, true);
    require(disabled[15 * 640 + 290] == gateway::debug_hotspot_color(222),
            "raw RGN mode can reveal even a native-disabled rectangle");
    // A validated live native picker supersedes the host-only object
    // location heuristic. Otherwise a real selectable object can disappear.
    debug.scene.boxes[2].presence = gateway::DebugHitboxPresence::absent;
    debug.scene.boxes[2].native_picker_disabled = false;
    std::vector<std::uint32_t> live_picker(640 * 480, 0xff123456);
    gateway::draw_debug_overlay(live_picker, 640, 480, mouse, debug, false, true, 0);
    require(live_picker[15 * 640 + 290] == gateway::debug_hotspot_color(222),
            "validated unskipped picker rectangle must remain visible");
    // Original picker's bounds are one pixel to the right of the
    // approximate RGN coordinate conversion.
    debug.scene.native_picker_aligned = true;
    gateway::Mouse shifted_mouse;
    shifted_mouse.x = 261;
    shifted_mouse.y = 15;
    std::vector<std::uint32_t> aligned(640 * 480, 0xff123456);
    gateway::draw_debug_overlay(aligned, 640, 480, shifted_mouse, debug, false, true, 0);
    require(aligned[15 * 640 + 260] == 0xff123456 &&
                aligned[10 * 640 + 261] == 0xffe6edfa,
            "original native picker x=261 inclusive bounds are not aligned");
    gateway::DebugOverlayData ui_controls;
    ui_controls.scene.native_controls.push_back({30, 10, 40, 20, 0, 1, 18, 5, 0});
    ui_controls.scene.native_controls.push_back({60, 10, 70, 20, 4, 2, 10, 4, 0x80});
    ui_controls.scene.native_controls.push_back({100, 463, 116, 479, 3, 4, 7, 4, 1});
    gateway::Mouse control_mouse;
    control_mouse.x = 35;
    control_mouse.y = 15;
    std::vector<std::uint32_t> controls(640 * 480, 0xff123456);
    gateway::draw_debug_overlay(controls, 640, 480, control_mouse, ui_controls,
                                false, true, 0);
    require(controls[10 * 640 + 30] == 0xffe6edfa &&
                controls[15 * 640 + 35] != 0xff123456 &&
                controls[15 * 640 + 65] == 0xff123456 &&
                controls[470 * 640 + 100] == gateway::debug_hotspot_color(0x807),
            "active non-RGN UI controls should render but skipped controls should not");
    gateway::draw_debug_overlay(controls, 640, 480, control_mouse, ui_controls,
                                false, true, 0, {}, true);
    require(controls[10 * 640 + 60] == 0xff777e89,
            "raw mode should expose a disabled native control");
    gateway::DebugOverlayData fallback;
    fallback.scene.native_picker_decoded = true;
    fallback.scene.native_picker_aligned = true;
    fallback.scene.native_controls.push_back({-4, -8, 260, 136, 0, 0, 0, 1, 1});
    fallback.scene.native_controls.push_back({30, 10, 40, 20, 0, 1, 18, 5, 0});
    fallback.scene.native_picker_rows = fallback.scene.native_controls;
    require(fallback.scene.picker_at(2, 50)->slot == 0 &&
                fallback.scene.picker_at(35, 15)->slot == 1,
            "original picker fallbacks must be selectable and overridden by controls");
    gateway::Mouse fallback_mouse;
    fallback_mouse.x = 2;
    fallback_mouse.y = 50;
    std::vector<std::uint32_t> backdrop(640 * 480, 0xff123456);
    gateway::draw_debug_overlay(backdrop, 640, 480, fallback_mouse, fallback,
                                false, true, 0);
    require(backdrop[50 * 640] == 0xffe6edfa &&
                backdrop[30 * 640 + 20] != 0xff123456 &&
                backdrop[70 * 640 + 20] != 0xff123456,
            "signed, selectable group fallback should appear in F10 overlay");
    fallback.scene.native_picker_aligned = false;
    std::vector<std::uint32_t> unmapped(640 * 480, 0xff123456);
    gateway::draw_debug_overlay(unmapped, 640, 480, fallback_mouse, fallback,
                                false, true, 0);
    require(fallback.scene.picker_at(35, 15)->slot == 1 &&
                unmapped[10 * 640 + 30] == gateway::debug_hotspot_color(0x812),
            "unmapped native control geometry should remain visible");
    // The normal F10 view paints the *winning* native picker row at each
    // pixel, rather than outlining overlapping rectangles independently.
    gateway::DebugOverlayData winners;
    winners.scene.native_picker_decoded = true;
    winners.scene.native_picker_aligned = true;
    winners.scene.native_room_group = 0;
    winners.scene.boxes.push_back({40, 40, 60, 60, 123, "test object"});
    winners.scene.native_picker_rows = {
        {0, 0, 100, 100, 0, 0, 0, 1, 0},
        {40, 40, 60, 60, 0, 1, 1, 3, 0},
        {50, 50, 70, 70, 1, 1, 2, 5, 0},
        {20, 20, 30, 30, 2, 1, 3, 5, 0x80},
    };
    gateway::Mouse winner_mouse;
    winner_mouse.x = 55;
    winner_mouse.y = 55;
    std::vector<std::uint32_t> selected(640 * 480, 0xff123456);
    gateway::draw_debug_overlay(selected, 640, 480, winner_mouse, winners,
                                false, true, 0);
    require(selected[40 * 640 + 40] == gateway::debug_hotspot_color(123) &&
                selected[55 * 640 + 50] == 0xffe6edfa &&
                selected[25 * 640 + 25] == 0xff123456 &&
                selected[55 * 640 + 71] == 0xff648ca0,
            "native winner pixels must honor last-match priority, disabled rows,"
            " and original RGN object color");
    // A real guest call is distinct from the cursor's model-only hover.
    gateway::DebugPickerObservation observed;
    observed.guest_ms = 12000;
    observed.x = 55;
    observed.y = 55;
    observed.native_ax = 1;
    observed.native_outputs = {1, 1, 5};
    observed.model_ready = true;
    observed.predicted = winners.scene.picker_at(55, 55);
    require(gateway::debug_picker_verdict(observed) == gateway::DebugPickerVerdict::match,
            "guest result must match the original last-enabled rectangle prediction");
    winners.native_picker_monitoring = true;
    winners.native_picker = observed;
    winners.guest_ms = 12500;
    std::vector<std::uint32_t> captured(640 * 480, 0xff123456);
    gateway::draw_debug_overlay(captured, 640, 480, winner_mouse, winners,
                                false, true, 0);
    require(captured[55 * 640 + 55] == 0xff77efa8 &&
                captured[390 * 640 + 2] == 0xff75aaff,
            "F10 should mark an actual recent guest pick and draw its evidence panel");
    // Even with H off, the F10 menu must show the original result, rather
    // than implying the highlighted mouse-over row is native evidence.
    std::vector<std::uint32_t> menu_result(640 * 480, 0xff123456);
    gateway::draw_debug_overlay(menu_result, 640, 480, winner_mouse, winners,
                                true, false, 0);
    require(menu_result[390 * 640 + 2] == 0xff75aaff,
            "native picker result must be visible in the F10 menu with H off");
    observed.native_outputs = {1, 0, 1};
    require(gateway::debug_picker_verdict(observed) == gateway::DebugPickerVerdict::mismatch,
            "different original group/slot/kind must be labeled mismatch");
    winners.native_picker = observed;
    std::vector<std::uint32_t> disagreement(640 * 480, 0xff123456);
    gateway::draw_debug_overlay(disagreement, 640, 480, winner_mouse, winners,
                                false, true, 0);
    require(disagreement[55 * 640 + 55] == 0xffff7777,
            "disagreement must visibly distinguish real native and predicted picks");
    observed.model_ready = false;
    require(gateway::debug_picker_verdict(observed) == gateway::DebugPickerVerdict::unmodeled,
            "unknown live table must not be misreported as an incorrect prediction");
    observed.model_ready = true;
    observed.predicted.reset();
    observed.native_ax = 0;
    observed.native_outputs = {0xffff, 0xffff, 0};
    require(gateway::debug_picker_verdict(observed) == gateway::DebugPickerVerdict::match,
            "native no-match result with sentinel outputs must compare correctly");
    observed.native_ax = 1;
    require(gateway::debug_picker_verdict(observed) == gateway::DebugPickerVerdict::mismatch,
            "original AX must be compared, not only the three output words");
    observed.native_ax = 0;
    winners.native_picker = observed;
    winners.guest_ms = 20000;
    std::vector<std::uint32_t> old_result(640 * 480, 0xff123456);
    gateway::draw_debug_overlay(old_result, 640, 480, winner_mouse, winners,
                                false, true, 0);
    require(old_result[55 * 640 + 55] != 0xff77efa8 &&
                old_result[390 * 640 + 2] == 0xff75aaff,
            "last native result remains visible but stale position marker expires");
    winners.native_picker_error = "unreadable guest output";
    std::vector<std::uint32_t> capture_error(640 * 480, 0xff123456);
    gateway::draw_debug_overlay(capture_error, 640, 480, winner_mouse, winners,
                                false, true, 0);
    require(capture_error[390 * 640 + 2] == 0xff75aaff &&
                capture_error[55 * 640 + 55] != 0xff77efa8,
            "failed observation must show a capture error instead of a stale success");
    gateway::draw_debug_overlay(pixels, 640, 480, mouse, debug, true, false, 4);
    require(pixels[232 * 640 + 286] == 0xff75aaff &&
                pixels[100 * 640 + 500] == 0xff123456,
            "default debug menu must stay below the scene image");
    std::vector<std::uint32_t> relocated(640 * 480, 0xff123456);
    gateway::draw_debug_overlay(relocated, 640, 480, mouse, debug, true, false, 0, moved);
    require(relocated[50 * 640 + 40] == 0xff75aaff &&
                relocated[232 * 640 + 500] == 0xff123456,
            "menu position must be movable without leaving another panel behind");
    gateway::DebugOverlayData polygon_debug;
    polygon_debug.scene.x_extent = 1;
    polygon_debug.scene.boxes.push_back({0, 0, 20, 20, 42, "polygon",
                                         {{0, 0}, {20, 0}, {20, 10},
                                          {10, 10}, {10, 20}, {0, 20}}, {}, {}});
    polygon_debug.scene.boxes.push_back({30, 0, 35, 5, 43, "two-point box", {}, {}, {}});
    std::vector<std::uint32_t> polygon_pixels(640 * 480, 0xff123456);
    gateway::draw_debug_overlay(polygon_pixels, 640, 480, mouse, polygon_debug, false, true, 0);
    require(polygon_pixels[15 * 640 + 265] != 0xff123456 &&
                polygon_pixels[15 * 640 + 275] == 0xff123456 &&
                polygon_pixels[5 * 640 + 275] != 0xff123456,
            "concave Gateway II polygon must not fill its missing corner");
    require(polygon_pixels[3 * 640 + 295] != 0xff123456 &&
                polygon_pixels[3 * 640 + 296] == 0xff123456,
            "Gateway II two-point region uses single-pixel X units");
    std::cout << "Gateway merged hotspot and draggable debug-menu raster checks passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
