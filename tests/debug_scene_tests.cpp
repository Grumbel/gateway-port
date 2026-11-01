#include "debug_scene.hpp"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
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
