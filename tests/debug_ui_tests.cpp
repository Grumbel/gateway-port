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
    require(controls[10 * 640 + 30] == 0xff123456 &&
                controls[15 * 640 + 35] == 0xff123456 &&
                controls[15 * 640 + 65] == 0xff123456 &&
                controls[470 * 640 + 100] == 0xff123456,
            "ordinary H view must not highlight regular GUI controls");
    gateway::draw_debug_overlay(controls, 640, 480, control_mouse, ui_controls,
                                false, true, 0, {}, true);
    require(controls[10 * 640 + 30] == 0xffe6edfa &&
                controls[10 * 640 + 60] == 0xff777e89 &&
                controls[470 * 640 + 100] == gateway::debug_hotspot_color(0x807),
            "raw R view retains active, disabled, and scrollbar UI controls");
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
                backdrop[10 * 640 + 30] == 0xff80c9ff &&
                backdrop[15 * 640 + 35] != 0xff123456,
            "H view must show the native fallback and winning UI controls");
    gateway::draw_debug_overlay(backdrop, 640, 480, fallback_mouse, fallback,
                                false, true, 0, {}, true);
    require(backdrop[50 * 640] == 0xffe6edfa &&
                backdrop[10 * 640 + 30] == gateway::debug_hotspot_color(0x812),
            "raw R view still exposes signed fallbacks and independent GUI rows");
    fallback.scene.native_picker_aligned = false;
    std::vector<std::uint32_t> unmapped(640 * 480, 0xff123456);
    gateway::draw_debug_overlay(unmapped, 640, 480, fallback_mouse, fallback,
                                false, true, 0);
    require(fallback.scene.picker_at(35, 15)->slot == 1 &&
                unmapped[10 * 640 + 30] == 0xff80c9ff,
            "unaligned native picker controls are still selectable and visible");
    // Adjacent native room slots merge by object ID, but later GUI rows
    // override their actual selection within the overlapping area.
    gateway::DebugOverlayData scene_only;
    scene_only.scene.native_picker_decoded = true;
    scene_only.scene.native_picker_aligned = true;
    scene_only.scene.native_room_group = 0;
    scene_only.scene.boxes = {
        {19, 40, 39, 60, 123, "merged object"},
        {40, 40, 60, 60, 123, "merged object"},
        {53, 45, 71, 65, 234, "other object"},
        {73, 40, 79, 60, 345, "disabled object"},
        {80, 40, 89, 60, 0, ""},
        {90, 40, 99, 60, 0, ""},
    };
    scene_only.scene.native_picker_rows = {
        {0, 0, 639, 479, 0, 0, 0, 1, 0}, // Scene fallback is not a hitbox.
        {280, 40, 300, 60, 0, 1, 1, 3, 0},
        {301, 40, 321, 60, 0, 2, 2, 3, 0},
        {314, 45, 332, 65, 0, 3, 3, 3, 0},
        {334, 40, 340, 60, 0, 4, 4, 3, 0x80},
        {341, 40, 350, 60, 0, 5, 6, 3, 0},
        {351, 40, 360, 60, 0, 6, 7, 3, 0},
        {285, 45, 312, 55, 1, 1, 5, 5, 0}, // Overlapping GUI row wins native scan.
    };
    gateway::Mouse room_mouse;
    room_mouse.x = 295;
    room_mouse.y = 43;
    require(scene_only.scene.picker_at(room_mouse.x, 50)->group == 1 &&
                scene_only.scene.picker_at(room_mouse.x, room_mouse.y)->group == 0,
            "fixture must have a competing GUI row above the room selection");
    std::vector<std::uint32_t> merged(640 * 480, 0xff123456);
    gateway::draw_debug_overlay(merged, 640, 480, room_mouse, scene_only,
                                false, true, 0);
    require(merged[43 * 640 + 280] == 0xffe6edfa &&
                merged[43 * 640 + 300] == merged[43 * 640 + 301] &&
                merged[43 * 640 + 300] != 0xffe6edfa &&
                merged[43 * 640 + 300] != 0xff123456,
            "adjacent native room slots of the same object form one hovered contour");
    require(merged[50 * 640 + 314] == gateway::debug_hotspot_color(234) &&
                merged[50 * 640 + 334] == 0xff123456 &&
                merged[50 * 640 + 270] == 0xff123456 &&
                merged[50 * 640 + 150] == 0xff123456,
            "winner map preserves object boundaries, skipped rows and broad fallback background");
    require(merged[50 * 640 + 350] == gateway::debug_hotspot_color(5) &&
                merged[50 * 640 + 351] == gateway::debug_hotspot_color(6),
            "anonymous room slots must retain separate outlines without a shared object ID");
    room_mouse.y = 50;
    std::vector<std::uint32_t> competing(640 * 480, 0xff123456);
    gateway::draw_debug_overlay(competing, 640, 480, room_mouse, scene_only,
                                false, true, 0);
    require(competing[50 * 640 + 285] == 0xffe6edfa &&
                competing[50 * 640 + 295] != 0xff123456 &&
                competing[50 * 640 + 295] != gateway::debug_hotspot_color(123) &&
                competing[50 * 640 + 314] == gateway::debug_hotspot_color(234),
            "a later UI picker row must visibly override the room object's contour");
    // The observation model independently checks native calls against the
    // same all-group ordering now used by the winner overlay.
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
                selected[55 * 640 + 71] == 0xff648ca0 &&
                selected[55 * 640 + 75] == 0xff123456,
            "native winner map must show room and UI but not tint broad fallbacks");
    auto covered = winners;
    covered.scene.native_picker_rows.push_back({42, 42, 48, 48, 3, 0, 4, 1, 0});
    gateway::Mouse covered_mouse;
    covered_mouse.x = 45;
    covered_mouse.y = 45;
    require(covered.scene.picker_at(45, 45)->group == 3,
            "last group fallback must override an earlier room candidate");
    std::vector<std::uint32_t> covered_pixels(640 * 480, 0xff123456);
    gateway::draw_debug_overlay(covered_pixels, 640, 480, covered_mouse, covered,
                                false, true, 0);
    require(covered_pixels[42 * 640 + 42] == 0xffe6edfa &&
                covered_pixels[45 * 640 + 45] != 0xff123456 &&
                covered_pixels[45 * 640 + 45] != gateway::debug_hotspot_color(123),
            "a later fallback also masks the room target and is identifiable on hover");
    // A real guest call is distinct from the cursor's model-only hover.
    gateway::DebugPickerObservation observed;
    observed.guest_ms = 12000;
    observed.x = 55;
    observed.y = 55;
    observed.native_ax = 1;
    observed.native_outputs = {1, 1, 5};
    observed.model_ready = true;
    observed.predicted = winners.scene.picker_at(55, 55);
    gateway::debug_picker_resolve_source(observed, winners.scene);
    require(gateway::debug_picker_verdict(observed) == gateway::DebugPickerVerdict::match,
            "guest result must match the original last-enabled rectangle prediction");
    using PickerSource = gateway::DebugPickerObservation::Source;
    require(observed.source == PickerSource::control && observed.native_row &&
                observed.native_row->index == 2 && observed.enabled_overlaps == 3 &&
                observed.native_object_id == 0,
            "native control provenance must use returned group/slot and count masked rows");
    auto classified = observed;
    classified.x = 45;
    classified.y = 45;
    classified.native_outputs = {0, 1, 3};
    gateway::debug_picker_resolve_source(classified, winners.scene);
    require(classified.source == PickerSource::room && classified.native_object_id == 123 &&
                classified.native_row && classified.native_row->index == 1 &&
                classified.enabled_overlaps == 2,
            "only the aligned room group can assign a native RGN object ID");
    classified.x = 10;
    classified.y = 10;
    classified.native_outputs = {0, 0, 1};
    gateway::debug_picker_resolve_source(classified, winners.scene);
    require(classified.source == PickerSource::fallback && classified.enabled_overlaps == 1 &&
                classified.native_row && classified.native_row->index == 0,
            "the picker's real slot-zero fallback is selectable and independently labeled");
    classified.x = 25;
    classified.y = 25;
    classified.native_outputs = {2, 1, 5};
    gateway::debug_picker_resolve_source(classified, winners.scene);
    require(classified.source == PickerSource::unresolved && classified.native_row &&
                classified.native_row->flags == 0x80 && classified.enabled_overlaps == 1,
            "native output naming a skipped row must not be claimed as an active control");
    classified.x = 75;
    classified.y = 55;
    classified.native_outputs = {0, 1, 3};
    gateway::debug_picker_resolve_source(classified, winners.scene);
    require(classified.source == PickerSource::unresolved && classified.native_object_id == 0,
            "row whose inclusive bounds exclude the call must be marked unresolved");
    classified.x = 45;
    classified.y = 45;
    auto unmatched_scene = winners.scene;
    unmatched_scene.native_picker_aligned = false;
    gateway::debug_picker_resolve_source(classified, unmatched_scene);
    require(classified.source == PickerSource::unmapped && classified.native_object_id == 0,
            "uncorrelated PIC/RGN scene must not acquire a guessed object identity");
    classified.native_ax = 0;
    classified.native_outputs = {0xffff, 0xffff, 0};
    gateway::debug_picker_resolve_source(classified, unmatched_scene);
    require(classified.source == PickerSource::none && !classified.native_row &&
                classified.enabled_overlaps == 2,
            "native no-match return must be recorded even when the model sees candidates");
    gateway::debug_picker_resolve_source(classified, {});
    require(classified.source == PickerSource::unmodeled && !classified.native_row &&
                classified.enabled_overlaps == 0,
            "missing picker memory must not inherit provenance from the preceding call");
    winners.native_picker_monitoring = true;
    winners.downstream_monitoring = true;
    winners.native_input_monitoring = true;
    winners.native_picker = observed;
    winners.guest_ms = 12500;
    std::vector<std::uint32_t> captured(640 * 480, 0xff123456);
    gateway::draw_debug_overlay(captured, 640, 480, winner_mouse, winners,
                                false, true, 0);
    require(captured[55 * 640 + 55] == 0xff77efa8 &&
                captured[362 * 640 + 2] == 0xff75aaff,
            "F10 should mark an actual recent guest pick and draw its evidence panel");
    // Even with H off, the F10 menu must show the original result, rather
    // than implying the highlighted mouse-over row is native evidence.
    std::vector<std::uint32_t> menu_result(640 * 480, 0xff123456);
    gateway::draw_debug_overlay(menu_result, 640, 480, winner_mouse, winners,
                                true, false, 0);
    require(menu_result[362 * 640 + 2] == 0xff75aaff,
            "native picker result must be visible in the F10 menu with H off");
    observed.downstream_candidate = gateway::DebugPickerObservation::DispatchCandidate{
        true, 0x71, 0, 0, 0x2c24, 0x1c59, 24};
    winners.native_picker = observed;
    std::vector<std::uint32_t> candidate(640 * 480, 0xff123456);
    gateway::draw_debug_overlay(candidate, 640, 480, winner_mouse, winners,
                                true, false, 0);
    bool candidate_visible = false;
    for (int y = 414; y < 422; ++y)
        for (int x = 8; x < 283; ++x)
            candidate_visible |= candidate[y * 640 + x] != menu_result[y * 640 + x];
    require(candidate_visible, "F10 panel must expose downstream action candidates");
    observed.downstream_candidate.reset();
    observed.recent_input = gateway::DebugPickerObservation::GuestMouseRead{
        12, 3, 5, 0, 0, 1, 55, 55, true};
    winners.native_picker = observed;
    std::vector<std::uint32_t> mouse_read(640 * 480, 0xff123456);
    gateway::draw_debug_overlay(mouse_read, 640, 480, winner_mouse, winners,
                                true, false, 0);
    bool mouse_read_visible = false;
    for (int y = 428; y < 436; ++y)
        for (int x = 8; x < 283; ++x)
            mouse_read_visible |= mouse_read[y * 640 + x] != menu_result[y * 640 + x];
    require(mouse_read_visible, "F10 must distinguish a real guest mouse read");
    observed.recent_input.reset();
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
                old_result[362 * 640 + 2] == 0xff75aaff,
            "last native result remains visible but stale position marker expires");
    winners.native_picker_error = "unreadable guest output";
    std::vector<std::uint32_t> capture_error(640 * 480, 0xff123456);
    gateway::draw_debug_overlay(capture_error, 640, 480, winner_mouse, winners,
                                false, true, 0);
    require(capture_error[362 * 640 + 2] == 0xff75aaff &&
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
