#include "debug_state.hpp"
#include <cstdint>
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
void store(std::vector<std::uint8_t> &m, unsigned seg, unsigned off, unsigned val) {
    const auto address = seg * 16 + off;
    m.at(address) = val & 255;
    m.at(address + 1) = val >> 8;
}
} // namespace

int main(int argc, char **argv) try {
    std::vector<std::uint8_t> memory(0x50000);
    constexpr unsigned ds = 0x1000, dir = 0x1200, desc = 0x1300;
    constexpr unsigned count_seg = 0x1400, record_seg = 0x1500;
    store(memory, ds, 0x8d50, dir);
    store(memory, ds, 0x8d52, desc);
    store(memory, ds, 0x8d54, count_seg);
    store(memory, count_seg, 0x38dc, 1);
    memory[desc * 16 + 5] = 36; // Type 2's size; handler at +22h.
    memory[dir * 16 + 0xc0] = 2;
    store(memory, dir, 0xc2, 0x200);
    store(memory, dir, 0xc4, record_seg);
    store(memory, record_seg, 0x200, 1); // One-based name token.
    store(memory, record_seg, 0x20e, 0x00ee);
    store(memory, record_seg, 0x222, 233);
    const auto state = gateway::inspect_gateway_state(memory, ds);
    require(state && state->objects.size() == 1 && state->objects[0].type == 2,
            "Gateway object directory recognition");
    require(state->objects[0].handler == 233 && state->objects[0].location == 0x00ee &&
                state->objects[0].raw.size() == 36,
            "Gateway handler, record size and location decoding");
    const std::vector<std::uint8_t> names{5, 0, 'B', 'o', 'o', 'k', 0};
    const auto dump = gateway::debug_objects_tsv(*state, names);
    require(dump.find("Book") != std::string::npos &&
                dump.find("233") != std::string::npos,
            "Gateway TSV names and handlers");
    const auto object_names = gateway::debug_engine_object_names(*state, names);
    require(object_names.size() == 2 && object_names[0].empty() && object_names[1] == "Book",
            "engine ID resolves the one-based OBJECT.DAT byte-offset token");
    memory[record_seg * 16 + 0x200] = 0;
    memory[record_seg * 16 + 0x201] = 0;
    store(memory, record_seg, 0x202, 1);
    const auto secondary = gateway::inspect_gateway_state(memory, ds);
    require(secondary && gateway::debug_engine_object_names(*secondary, names)[1] == "Book",
            "unnamed object falls back to secondary token");
    memory[dir * 16 + 0xc0] = 9;
    require(!gateway::inspect_gateway_state(memory, ds), "invalid object type accepted");
    require(!gateway::inspect_gateway_state(memory, 0), "unrelated DS recognized as Gateway");

    // A newer/older Legend game may relocate the whole directory and DS
    // globals. Resolve ID->name tokens from a validated directory rather than
    // indexing OBJECT.DAT strings directly (aliases shift those indexes).
    std::vector<std::uint8_t> other_memory(0xa0000);
    std::vector<std::uint8_t> other_names(2);
    constexpr unsigned other_dir = 0x18001, other_record_seg = 0x3000;
    auto absolute_word = [&](unsigned at, unsigned value) {
        other_memory.at(at) = value & 255;
        other_memory.at(at + 1) = value >> 8;
    };
    for (unsigned id = 1; id <= 320; ++id) {
        const auto entry = other_dir + 6 * (id - 1);
        const auto record = 0x200 + 8 * id;
        const unsigned token = other_names.size() - 1;
        const auto label = "Object " + std::to_string(id);
        other_names.insert(other_names.end(), label.begin(), label.end());
        other_names.push_back(0);
        unsigned alias = 0;
        if (id == 1) {
            alias = other_names.size() - 1;
            const std::string also = "first alias";
            other_names.insert(other_names.end(), also.begin(), also.end());
            other_names.push_back(0);
        }
        other_memory[entry] = 2;
        absolute_word(entry + 2, record);
        absolute_word(entry + 4, other_record_seg);
        absolute_word(other_record_seg * 16 + record, token);
        absolute_word(other_record_seg * 16 + record + 2, alias);
    }
    other_names[0] = (other_names.size() - 2) & 255;
    other_names[1] = (other_names.size() - 2) >> 8;
    const auto found = gateway::inspect_legend_name_table(other_memory, other_names, 300);
    require(found && found->directory_address == other_dir && found->names.size() == 321 &&
                found->names[1] == "Object 1" && found->names[2] == "Object 2" &&
                found->names[292] == "Object 292",
            "relocated Legend directory maps IDs through name tokens despite extra aliases");
    require(gateway::inspect_legend_name_table(other_memory, other_names, 300,
                                               found->directory_address)->names[292] ==
                "Object 292",
            "reusing a validated directory hint");
    require(!gateway::inspect_legend_name_table(other_memory, other_names, 321) &&
                !gateway::inspect_legend_name_table(other_memory, {}),
            "invalid ID range or missing name resource must not produce labels");
    absolute_word(other_record_seg * 16 + 0x200 + 8 * 100, 0xffff);
    require(!gateway::inspect_legend_name_table(other_memory, other_names, 300),
            "partial/untrusted name directory must not produce misleading labels");
    if (argc > 2) {
        // Optional user-provided Gateway II live snapshot and original
        // OBJECT.DAT. Both remain outside the repository.
        auto read = [](const char *filename) {
            std::ifstream file(filename, std::ios::binary);
            require(bool(file), "missing optional Gateway II name fixture");
            return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), {});
        };
        const auto snapshot = read(argv[1]);
        const auto object_dat = read(argv[2]);
        const auto actual = gateway::inspect_legend_name_table(snapshot, object_dat, 520);
        require(actual && actual->directory_address == 0x38b1d &&
                    actual->names.size() == 522 && actual->names[7] == "ground" &&
                    actual->names[377] == "windows" &&
                    actual->names[520] == "laser rifle",
                "Gateway II live 521-object table with odd-aligned directory");
        std::cout << "Gateway II live object-name snapshot checks passed\n";
    }
    std::cout << "Gateway live object snapshot checks passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
