// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "picker_scene_trace.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
void require(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
} // namespace

int main() try {
    gateway::DebugSceneView scene;
    scene.picture = "GATE_102.PIC";
    scene.entry = 15;
    scene.native_picker_aligned = true;
    scene.native_room_group = 0;
    scene.boxes = {{19, 40, 39, 60, 123, "first", {},
                    gateway::DebugHitboxPresence::unknown, {}, {}},
                   {40, 40, 60, 60, 123, "second", {},
                    gateway::DebugHitboxPresence::unknown, {}, {}}};
    scene.native_picker_rows = {
        {0, 0, 639, 479, 0, 0, 0, 1, 0},
        {280, 40, 300, 60, 0, 1, 1, 3, 0},
        {301, 40, 321, 60, 0, 2, 2, 3, 0x80},
        {285, 45, 312, 55, 1, 1, 5, 5, 0},
    };
    std::ostringstream out;
    gateway::write_picker_scene_trace(out, 7, scene, 1, {1, 1, 5});
    const auto data = out.str();
    require(data.starts_with("# scene call=7 picture=GATE_102.PIC entry=15 aligned=1 "
                             "room_group=0 boxes=2 rows=4\n"),
            "aligned scene identity must be captured at picker entry");
    require(data.find("group=0 slot=0 index=0 kind=1 flags=0 x0=0 y0=0 "
                      "x1=639 y1=479 role=fallback object=0 selected=0\n") !=
                std::string::npos,
            "native fallback row must be independent of room geometry");
    require(data.find("group=0 slot=1 index=1 kind=3 flags=0 x0=280 y0=40 "
                      "x1=300 y1=60 role=room object=123 selected=0\n") !=
                std::string::npos,
            "original room object identity must survive tracing");
    require(data.find("group=0 slot=2 index=2 kind=3 flags=128 x0=301 y0=40 "
                      "x1=321 y1=60 role=room object=123 selected=0\n") !=
                std::string::npos,
            "disabled adjacent rectangle must retain object identity and skip bit");
    require(data.find("group=1 slot=1 index=5 kind=5 flags=0 x0=285 y0=45 "
                      "x1=312 y1=55 role=control object=0 selected=1\n") !=
                std::string::npos,
            "actual selected GUI row must not acquire a room object ID");
    scene.native_picker_aligned = false;
    scene.picture = "GATE_102.PIC extra untrusted";
    out.str({});
    out.clear();
    gateway::write_picker_scene_trace(out, 8, scene, 0, {0xffff, 0xffff, 0});
    const auto unknown = out.str();
    require(unknown.starts_with("# scene call=8 picture=- entry=15 aligned=0 "
                                 "room_group=-1 boxes=2 rows=4\n"),
            "unsafe filename must be sanitized and ambiguous room group withheld");
    require(unknown.find("role=unmapped object=0 selected=0\n") != std::string::npos &&
                unknown.find("role=room ") == std::string::npos,
            "unmatched RGN must not assign identities to native rows");
    std::cout << "Gateway picker scene trace serialization checks passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
