// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#pragma once
#include "debug_scene.hpp"
#include "mouse.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace gateway {
struct DebugActions {
    bool dump_state = false;
    bool toggle_auto_follow = false;
    int scene_step = 0;
};
// The last completed call to the original Gateway 1 rectangle picker, not a
// hover prediction or evidence that the later action dispatcher accepted it.
struct DebugPickerObservation {
    unsigned guest_ms = 0;
    int x = 0, y = 0;
    std::uint16_t native_ax = 0;
    std::array<std::uint16_t, 3> native_outputs{}; // group, slot, kind
    bool model_ready = false;
    std::optional<DebugPickerControl> predicted;
    // Resolved from the live table captured at entry, using the *native*
    // outputs after return, not by trusting the predicted winning row.
    enum class Source { unmodeled, none, fallback, room, control, unmapped, unresolved };
    Source source = Source::unmodeled;
    std::optional<DebugPickerControl> native_row;
    std::uint16_t native_object_id = 0; // Only a fully aligned room RGN association.
    unsigned enabled_overlaps = 0;
    struct DispatchCandidate {
        bool action_table = false;
        std::uint16_t action = 0, first = 0, second = 0;
        std::uint16_t target_cs = 0, target_ip = 0;
        unsigned age_ms = 0;
    };
    // Last matching resident dispatch site within the bounded post-pick
    // window. Temporal observation, NOT proof this click caused the dispatch.
    std::optional<DispatchCandidate> downstream_candidate;
    struct GuestMouseRead {
        unsigned seq = 0, age_ms = 0, function = 0;
        int button = -1;
        unsigned buttons = 0, count = 0;
        int x = 0, y = 0;
        bool coordinates_match = false;
    };
    std::optional<GuestMouseRead> recent_input;
};
enum class DebugPickerVerdict { match, mismatch, unmodeled };
DebugPickerVerdict debug_picker_verdict(const DebugPickerObservation &observation);
void debug_picker_resolve_source(DebugPickerObservation &observation,
                                 const DebugSceneView &entry_scene);
const char *debug_picker_source_name(DebugPickerObservation::Source source);
struct DebugOverlayData {
    DebugSceneView scene;
    std::string registers;
    std::string object_summary;
    std::string dump_message;
    unsigned guest_ms = 0;
    bool native_picker_monitoring = false;
    bool downstream_monitoring = false;
    bool native_input_monitoring = false;
    std::optional<DebugPickerObservation> native_picker;
    std::string native_picker_error;
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
