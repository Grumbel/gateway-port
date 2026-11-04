// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "debug_scene.hpp"
#include "debug_state.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace {
void require(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
void word(std::vector<std::uint8_t> &bytes, std::size_t offset, unsigned value) {
    bytes[offset] = value & 255;
    bytes[offset + 1] = value >> 8;
}
void entry(std::vector<std::uint8_t> &bytes, int index, int offset, int flags) {
    const auto start = index * 12;
    word(bytes, start, offset);
    word(bytes, start + 2, 0);
    word(bytes, start + 4, flags);
    word(bytes, start + 6, 379);
    word(bytes, start + 8, 224);
}
} // namespace

int main(int argc, char **argv) try {
    std::vector<std::uint8_t> pic(48);
    entry(pic, 0, 36, 0x2300);
    entry(pic, 1, 40, 0x4300); // XOR: not a new base scene.
    entry(pic, 2, 44, 0x2300);
    std::vector<std::uint8_t> rgn(36);
    for (int i = 0; i < 3; ++i) {
        word(rgn, i * 6, 18 + i * 6);
        word(rgn, i * 6 + 2, 9);
        word(rgn, i * 6 + 4, 1);
        word(rgn, 18 + i * 6, i + 1);
        word(rgn, 20 + i * 6, 0x0702);
        word(rgn, 22 + i * 6, 0x0906);
    }
    gateway::DebugSceneTracker tracker({"", "One", "Two", "Three"});
    require(gateway::DebugSceneTracker::matching_rgn("c:\\gate_102.pic") == "GATE_002.RGN",
            "Gateway room-picture RGN mapping");
    require(gateway::DebugSceneTracker::matching_rgn("GATE_315.PIC") == "GATE_315.RGN",
            "non-1xx RGN mapping");
    require(gateway::DebugSceneTracker::matching_rgn("C:S301_107.PIC") == "S301_007.RGN" &&
                gateway::DebugSceneTracker::matching_rgn("HOME_115.PIC") == "HOME_015.RGN" &&
                gateway::DebugSceneTracker::matching_rgn("HOME_304.PIC") == "HOME_304.RGN",
            "Spellcasting 301 and Gateway II numbered room mapping");
    require(gateway::DebugSceneTracker::matching_rgn("S101_E3.PIC") == "S101_R3.RGN" &&
                gateway::DebugSceneTracker::matching_rgn("s201_c7.pic") == "S201_R7.RGN" &&
                gateway::DebugSceneTracker::matching_rgn("INTRO.PIC").empty(),
            "early Spellcasting VGA/CGA room mapping and unrelated PIC rejection");
    require(tracker.add_picture("C:\\GATE_102.PIC", pic, rgn), "register Gateway picture");
    tracker.note_read("c:\\gate_102.pic", 0, 36);
    require(tracker.view().picture.empty(), "reading PIC directory chose a scene");
    tracker.note_read("C:GATE_102.PIC", 38, 2);
    require(tracker.view().entry == 0 && tracker.view().boxes[0].name == "One",
            "reading first room picture selected its RGN hitbox");
    tracker.note_read("GATE_102.PIC", 41, 2);
    require(tracker.view().entry == 0, "XOR overlay incorrectly replaced base frame");
    tracker.note_read("GATE_102.PIC", 45, 2);
    require(tracker.view().entry == 2, "second room picture not selected");
    tracker.step(-1);
    require(tracker.view().entry == 1 && !tracker.view().auto_follow,
            "manual frame inspection should stop automatic following");
    tracker.note_read("GATE_102.PIC", 38, 2);
    require(tracker.view().entry == 1, "manual frame selection overwritten by file read");
    tracker.auto_follow(true);
    tracker.note_read("GATE_102.PIC", 38, 2);
    require(tracker.view().entry == 0, "resuming automatic scene tracking");
    tracker.set_object_names({"", "Engine object one", "Engine object two"});
    require(tracker.view().boxes[0].name == "Engine object one" &&
                tracker.view().boxes[0].object_id == 1,
            "scene labels follow native engine object IDs, not string-list positions");
    require(!tracker.view().boxes[0].native_picker_disabled,
            "without a guest rectangle match the picker mask must be unknown");
    // Synthetic Gateway 1 guest picker: one picture group bounding box and
    // one RGN rectangle, matching the native CS:IP 2649:0541 index lists.
    std::vector<std::uint8_t> picker_memory(1024 * 1024);
    constexpr std::size_t picker_base = 0x20000;
    word(picker_memory, picker_base + 0x8cc6, 0x2000);
    word(picker_memory, picker_base + 0x1576, 2); // room group + separate UI controls
    picker_memory[picker_base + 0xa8d4] = 2;    // bounding box + one RGN box
    picker_memory[picker_base + 0xa974] = 0;
    picker_memory[picker_base + 0xa975] = 1;
    picker_memory[picker_base + 0xaa4e] = 1; // non-selectable native group bound
    const auto box = tracker.view().boxes[0];
    word(picker_memory, picker_base + 0xa6d2, box.x0 + 261);
    word(picker_memory, picker_base + 0xa752, box.x1 + 261);
    word(picker_memory, picker_base + 0xa7d2, box.y0);
    word(picker_memory, picker_base + 0xa852, box.y1);
    picker_memory[picker_base + 0xaa4f] = 3; // native picture-region selector
    picker_memory[picker_base + 0xa921] = 0x80;
    // The native picker also has controls absent from PIC/RGN data. A second
    // group with a kind-1 bounding rectangle and two kind-5 controls.
    picker_memory[picker_base + 0xa8d5] = 3;
    picker_memory[picker_base + 0xa974 + 35] = 2;
    picker_memory[picker_base + 0xa974 + 36] = 3;
    picker_memory[picker_base + 0xa974 + 37] = 4;
    picker_memory[picker_base + 0xaa50] = 1; // non-selectable group bounding box
    for (unsigned index : {3u, 4u}) {
        word(picker_memory, picker_base + 0xa6d0 + index * 2, index * 10);
        word(picker_memory, picker_base + 0xa750 + index * 2, index * 10 + 6);
        word(picker_memory, picker_base + 0xa7d0 + index * 2, 10);
        word(picker_memory, picker_base + 0xa850 + index * 2, 20);
        picker_memory[picker_base + 0xaa4e + index] = 5;
    }
    picker_memory[picker_base + 0xa924] = 0x80; // native-disabled control
    tracker.set_native_picker_state(picker_memory, 0x2000);
    require(tracker.view().boxes[0].native_picker_disabled == true,
            "matched rectangle with native 0x80 skip bit is disabled");
    require(tracker.view().native_picker_aligned &&
                tracker.view().native_controls.size() == 2 &&
                tracker.view().native_controls[0].x0 == 30 &&
                tracker.view().native_controls[0].kind == 5 &&
                tracker.view().native_controls[1].flags == 0x80,
            "native picker must expose non-RGN screen controls and their skip bits");
    picker_memory[picker_base + 0xa921] = 0;
    tracker.set_native_picker_state(picker_memory, 0x2000);
    require(tracker.view().boxes[0].native_picker_disabled == false,
            "clear native 0x80 skip bit does not disable this rectangle");
    word(picker_memory, picker_base + 0xa6d2, box.x0 + 260);
    tracker.set_native_picker_state(picker_memory, 0x2000);
    require(!tracker.view().boxes[0].native_picker_disabled,
            "mismatched native geometry must not assign a picker mask");
    require(!tracker.view().native_picker_aligned && tracker.view().native_controls.empty(),
            "unverified room geometry must not expose arbitrary guest control bytes");
    tracker.step(1);
    require(!tracker.view().boxes[0].native_picker_disabled,
            "the picker mask belongs only to the matched room entry");
    tracker.auto_follow(true);
    tracker.note_read("GATE_102.PIC", 38, 1);
    // RGN lists potential targets. Guest containment excludes off-room and
    // carried objects; a closed drawer excludes its nested book. Even a room
    // match is a *candidate*, never proof that the native renderer drew it.
    auto conditional_rgn = rgn;
    word(conditional_rgn, 18, 0x123); // book in closed drawer
    word(conditional_rgn, 24, 0x130); // object in another room
    word(conditional_rgn, 30, 0x124); // drawer itself
    gateway::DebugSceneTracker conditional;
    require(conditional.add_picture("GATE_102.PIC", pic, conditional_rgn),
            "register conditional room regions");
    conditional.note_read("GATE_102.PIC", 38, 1);
    require(conditional.view().boxes[0].presence == gateway::DebugHitboxPresence::unknown,
            "uninspected RGN target must not be presented as visible");
    gateway::DebugGameState live;
    live.objects.resize(734);
    for (std::size_t i = 0; i < live.objects.size(); ++i) {
        live.objects[i].id = std::uint16_t(i + 1);
        live.objects[i].type = 3;
    }
    live.objects[0xee - 1].type = 1;
    live.objects[0xf1 - 1].type = 1;
    auto &player = live.objects[0xd3 - 1];
    player.type = 7;
    player.location = 0xee;
    auto &drawer = live.objects[0x124 - 1];
    drawer.type = 2;
    drawer.location = 0xee;
    drawer.parser_gate_flags = 0; // All four restricted-slot flags read as false.
    auto &book = live.objects[0x123 - 1];
    book.type = 2;
    book.location = 0x124;
    book.disclosed = false;
    book.raw.resize(36);
    book.raw[0x10] = 0; // Indexed child slot zero in the drawer.
    auto &off_room = live.objects[0x130 - 1];
    off_room.type = 2;
    off_room.location = 0xf1;
    live.drawer_open = false;
    conditional.set_game_state(live);
    require(conditional.view().boxes[0].presence == gateway::DebugHitboxPresence::absent,
            "book inside closed drawer must be excluded from room candidates");
    require(conditional.view().boxes[0].restricted_parser_path == false,
            "closed drawer excludes slot zero from a restricted parser traversal");
    conditional.note_read("GATE_102.PIC", 45, 1);
    require(conditional.view().boxes[0].presence == gateway::DebugHitboxPresence::room_candidate,
            "closed drawer itself remains a room candidate");
    conditional.note_read("GATE_102.PIC", 38, 1);
    live.drawer_open = true;
    drawer.parser_gate_flags = 0x03; // Native 0x0c and 0x13.
    conditional.set_game_state(live);
    require(conditional.view().boxes[0].presence == gateway::DebugHitboxPresence::undisclosed,
            "a still-undisclosed book is not yet a live room target");
    require(conditional.view().boxes[0].restricted_parser_path == true,
            "open drawer allows slot zero in a restricted parser traversal");
    book.raw[0x10] = 1;
    conditional.set_game_state(live);
    require(conditional.view().boxes[0].restricted_parser_path == false,
            "parser gate tests child slot one independently of slot zero");
    drawer.parser_gate_flags = 0x0b; // Also native 0x1c.
    conditional.set_game_state(live);
    require(conditional.view().boxes[0].restricted_parser_path == true,
            "parent's native 0x1c flag enables child slot one");
    book.raw[0x10] = 0;
    drawer.parser_gate_flags.reset();
    conditional.set_game_state(live);
    require(!conditional.view().boxes[0].restricted_parser_path,
            "unreadable parent parser flags must not be treated as cleared");
    drawer.parser_gate_flags = 0x03;
    book.disclosed = true;
    conditional.set_game_state(live);
    require(conditional.view().boxes[0].presence == gateway::DebugHitboxPresence::room_candidate,
            "disclosed book in open drawer is a candidate, not confirmed drawn");
    book.disclosed.reset();
    conditional.set_game_state(live);
    require(conditional.view().boxes[0].presence == gateway::DebugHitboxPresence::room_candidate,
            "unsupported disclosure property must not be mistaken for false");
    book.location = 0x123;
    conditional.set_game_state(live);
    require(conditional.view().boxes[0].presence == gateway::DebugHitboxPresence::unknown,
            "cyclic location chains must terminate without inventing visibility");
    require(!conditional.view().boxes[0].restricted_parser_path,
            "cyclic containment must not invent a restricted parser answer");
    book.location = 0xd3;
    conditional.set_game_state(live);
    require(conditional.view().boxes[0].presence == gateway::DebugHitboxPresence::absent,
            "carried book cannot remain a room hotspot");
    conditional.note_read("GATE_102.PIC", 41, 1); // XOR overlay does not change the scene
    conditional.step(1); // Manually choose the second region frame.
    require(conditional.view().boxes[0].presence == gateway::DebugHitboxPresence::absent,
            "object in another room cannot be a current room target");
    off_room.type = 6;
    off_room.raw.resize(0x20);
    off_room.raw[0x0c] = 0xee; // special passage's *second* room endpoint
    conditional.set_game_state(live);
    require(conditional.view().boxes[0].presence == gateway::DebugHitboxPresence::room_candidate,
            "two-room passage must remain a candidate from its second endpoint");
    conditional.auto_follow(true);
    live.objects[0x2a1 - 1].type = 2;
    live.objects[0x2a1 - 1].location = 0xee;
    player.location = 0x2a1; // VR couch: player is nested inside the room.
    conditional.set_game_state(live);
    conditional.note_read("GATE_102.PIC", 45, 1);
    require(conditional.view().boxes[0].presence == gateway::DebugHitboxPresence::room_candidate,
            "current room must follow player through nested containers");
    player.location = 0; // Unknown scene: reject no candidate on guessed grounds.
    conditional.set_game_state(live);
    require(conditional.view().boxes[0].presence == gateway::DebugHitboxPresence::unknown,
            "missing player room must not create false absence claims");
    if (argc > 1) {
        const auto dir = std::filesystem::path(argv[1]);
        auto read = [&](const char *name) {
            std::ifstream stream(dir / name, std::ios::binary);
            require(bool(stream), "cannot open optional Gateway fixture");
            return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream), {});
        };
        const auto real_pic = read("GATE_102.PIC");
        const auto real_rgn = read("GATE_002.RGN");
        const auto names = gateway::parse_object_names(read("OBJECT.DAT"));
        require(names.at(292) == "slip of paper",
                "reference string-list entry demonstrates the old naming bug");
        auto real = gateway::DebugSceneTracker();
        require(real.add_picture("C:GATE_102.PIC", real_pic, real_rgn),
                "Gateway 1 PIC/RGN actual file pair");
        const auto entries = gateway::parse_pic_directory(real_pic);
        bool selected = false;
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (!entries[i].offset || (entries[i].flags & 0x4000) ||
                entries[i].width < 260 || entries[i].height < 150)
                continue;
            real.note_read("GATE_102.PIC", entries[i].offset, 1);
            const auto view = real.view();
            require(view.entry == i && view.entry_count == entries.size(),
                    "Gateway 1 real PIC/RGN frame correlation");
            selected = true;
            break;
        }
        require(selected, "Gateway real PIC has no suitable room frame");
        std::vector<std::string> recovered(735);
        recovered[292] = "drawer"; // Known native Gateway object 0x124.
        real.set_object_names(std::move(recovered));
        real.note_read("GATE_102.PIC", entries.at(15).offset, 1);
        const auto room = real.view();
        const auto drawer = std::find_if(room.boxes.begin(), room.boxes.end(),
                                         [](const auto &r) { return r.object_id == 292; });
        require(room.entry == 15 && drawer != room.boxes.end() && drawer->name == "drawer" &&
                    drawer->x0 == 90 && drawer->y0 == 162,
                "Gateway room 15 drawer is native object 292, not OBJECT.DAT name index 292");
        if (const auto *dump_dir = std::getenv("GATEWAY_PICKER_RAM_DIR")) {
            // Optional original-game RAM snapshots from the opening walkthrough.
            // The two overlapping drawer rectangles exchange native skip bits
            // when the drawer opens, while the RGN file itself stays unchanged.
            for (const auto &[stage, disabled_x0, disabled_y0] :
                 std::array<std::tuple<const char *, unsigned, unsigned>, 4>{
                     {{"before", 90, 158}, {"open", 90, 162},
                      {"taken", 90, 162}, {"closed", 90, 158}}}) {
                const auto dump = std::filesystem::path(dump_dir);
                std::ifstream guest_file(dump / (std::string(stage) + ".bin"),
                                         std::ios::binary);
                require(bool(guest_file), "cannot open original guest RAM checkpoint");
                const std::vector<std::uint8_t> guest(
                    std::istreambuf_iterator<char>(guest_file), {});
                std::ifstream metadata(dump / (std::string(stage) + ".debug.txt"));
                require(bool(metadata), "cannot open original guest RAM metadata");
                std::string line;
                unsigned guest_ds = 0;
                while (std::getline(metadata, line))
                    if (line.starts_with("object_ds="))
                        guest_ds = std::stoul(line.substr(10), nullptr, 16);
                require(guest_ds != 0 && guest_ds < 65536, "missing guest DS in metadata");
                real.set_native_picker_state(guest, std::uint16_t(guest_ds));
                const auto checkpoint = real.view();
                require(checkpoint.boxes.size() == 30,
                        "expected the original 30 first-room RGN rectangles");
                unsigned disabled_count = 0;
                for (const auto &box : checkpoint.boxes) {
                    require(box.native_picker_disabled.has_value(),
                            "all 30 room RGN records must match the native table");
                    if (*box.native_picker_disabled) {
                        ++disabled_count;
                        require(box.object_id == 0x124 && box.x0 == disabled_x0 &&
                                    box.y0 == disabled_y0,
                                "native drawer rectangle skip bit did not follow door state");
                    }
                }
                require(disabled_count == 1,
                        "exactly one first-room rectangle is natively disabled");
                // 14 command-side picker rows and four each in two scrollbar
                // groups were not represented in the room RGN file at all.
                require(checkpoint.native_picker_aligned &&
                            checkpoint.native_controls.size() == 22,
                        "expected all original non-RGN native picker controls");
                unsigned skipped_controls = 0;
                bool command_present = false, scroll_present = false;
                for (const auto &control : checkpoint.native_controls) {
                    skipped_controls += (control.flags & 0x80) != 0;
                    command_present |= control.group == 0 && control.kind == 5 &&
                                       control.x0 == 51 && control.y0 == 3;
                    scroll_present |= control.group == 4 && control.kind == 4 &&
                                      control.x0 == 244 && control.y0 == 152;
                }
                require(skipped_controls == 4 && command_present && scroll_present,
                        "native commands and scrollbar geometry/disabled bits changed");
            }
        }
    }
    if (argc > 2) {
        // Optional real game tree, e.g. the contents of games.zip. This also
        // verifies that every supported dialect's PIC and RGN agree on the
        // frame indices: a parser-only test cannot detect a bad file mapping.
        const auto root = std::filesystem::path(argv[2]);
        auto read = [&](const std::string &file) {
            std::ifstream stream(root / file, std::ios::binary);
            require(bool(stream), "cannot open optional other-game fixture");
            return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream), {});
        };
        gateway::DebugSceneTracker other;
        unsigned checked = 0, rooms = 0, polygons = 0;
        auto check_pair = [&](const std::string &folder, const std::string &pic) {
            const auto region = gateway::DebugSceneTracker::matching_rgn(pic);
            const auto picture_data = read(folder + '/' + pic);
            require(other.add_picture(pic, picture_data, read(folder + '/' + region)),
                    "cannot register real other-game PIC/RGN pair");
            ++checked;
            const auto entries = gateway::parse_pic_directory(picture_data);
            for (std::size_t i = 0; i < entries.size(); ++i) {
                const auto &entry = entries[i];
                if (!entry.offset || (entry.flags & 0x4000) ||
                    entry.width < 260 || entry.height < 150)
                    continue;
                other.note_read(pic, entry.offset + 1, 1);
                const auto view = other.view();
                require(view.picture == pic && view.entry == i &&
                            view.entry_count == entries.size(),
                        "real other-game room PIC read did not select matching RGN slot");
                if (view.boxes.empty())
                    continue;
                ++rooms;
                for (const auto &box : view.boxes)
                    if (!box.polygon.empty())
                        ++polygons;
            }
        };
        for (int n = 1; n <= 6; ++n) {
            check_pair("101", "S101_E" + std::to_string(n) + ".PIC");
            check_pair("101", "S101_C" + std::to_string(n) + ".PIC");
        }
        for (int n = 1; n <= 7; ++n) {
            check_pair("201", "S201_E" + std::to_string(n) + ".PIC");
            check_pair("201", "S201_C" + std::to_string(n) + ".PIC");
        }
        for (int n = 2; n <= 7; ++n)
            check_pair("301", "S301_10" + std::to_string(n) + ".PIC");
        for (int n = 2; n <= 7; ++n)
            check_pair("gateway2-dos", "HOME_10" + std::to_string(n) + ".PIC");
        check_pair("gateway2-dos", "HOME_115.PIC");
        check_pair("gateway2-dos", "HOME_304.PIC");
        check_pair("gateway2-dos", "HOME_315.PIC");
        require(checked == 41 && rooms > 100 && polygons > 100,
                "too few real other-game region frames decoded");
        std::cout << "Other-game region fixtures: " << checked << " pairs, " << rooms
                  << " room frames, " << polygons << " polygons\n";
    }
    std::cout << "Gateway scene-tracking checks passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
