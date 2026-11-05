// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "picker_scene_trace.hpp"
#include <algorithm>
#include <ostream>
#include <string>

namespace gateway {
void write_picker_scene_trace(std::ostream &output, unsigned call,
                              const DebugSceneView &scene, std::uint16_t native_ax,
                              std::array<std::uint16_t, 3> native_outputs) {
    // Space-free, inert filename token; a corrupted guest path must not
    // inject extra metadata fields into a machine-readable trace.
    const auto &name = scene.picture;
    const bool safe_name = !name.empty() &&
        std::all_of(name.begin(), name.end(), [](unsigned char c) {
            return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                   c == '_' || c == '-' || c == '.';
        });
    const auto room_group = scene.native_picker_aligned && scene.native_room_group
                                ? int(*scene.native_room_group) : -1;
    output << "# scene call=" << call << " picture=" << (safe_name ? name : "-")
           << " entry=" << scene.entry << " aligned=" << (room_group >= 0 ? 1 : 0)
           << " room_group=" << room_group << " boxes=" << scene.boxes.size()
           << " rows=" << scene.native_picker_rows.size() << '\n';
    for (const auto &row : scene.native_picker_rows) {
        const bool room = row.slot != 0 && room_group >= 0 &&
                          row.group == room_group && row.slot <= scene.boxes.size();
        const char *role = row.slot == 0 ? "fallback"
                           : room_group < 0 ? "unmapped"
                           : room ? "room" : "control";
        const auto object = room ? scene.boxes[row.slot - 1].object_id : 0;
        const bool selected = native_ax == 1 && native_outputs[0] == row.group &&
                              native_outputs[1] == row.slot && native_outputs[2] == row.kind;
        output << "# picker-row call=" << call << " group=" << unsigned(row.group)
               << " slot=" << unsigned(row.slot) << " index=" << unsigned(row.index)
               << " kind=" << unsigned(row.kind) << " flags=" << unsigned(row.flags)
               << " x0=" << row.x0 << " y0=" << row.y0
               << " x1=" << row.x1 << " y1=" << row.y1
               << " role=" << role << " object=" << object
               << " selected=" << (selected ? 1 : 0) << '\n';
    }
}
} // namespace gateway
