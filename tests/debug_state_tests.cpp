#include "debug_state.hpp"
#include <cstdint>
#include <iostream>
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

int main() try {
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
    std::cout << "Gateway live object snapshot checks passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
