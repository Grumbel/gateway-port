#include "object_flags.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>

void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
int main() {
    try {
        std::vector<std::uint8_t> memory(0xa0000);
        const auto put = [&](unsigned segment, unsigned offset, std::uint16_t value) {
            memory.at(segment * 16 + offset) = value & 255;
            memory.at(segment * 16 + std::uint16_t(offset + 1)) = value >> 8;
        };
        const gateway::GuestByteReader read = [&](std::uint16_t segment, std::uint16_t offset) {
            return memory.at(segment * 16 + offset);
        };
        constexpr unsigned ds = 0x1000, directory = 0x2000, properties = 0x3000, data = 0x4000;
        put(ds, 0x8d50, directory);
        put(ds, 0x8d54, ds);
        put(ds, 0x8d56, properties);
        put(ds, 0x38dc, 10);
        // Object 2, property 3: flag bit 13, valid for all eight record types.
        put(directory, 2 * 6 + 0xbc, 0x120);
        put(directory, 2 * 6 + 0xbe, data);
        memory[properties * 16 + 3 * 4 + 0x14] = 13;
        put(properties, 3 * 4 + 0x16, 0xff);
        memory[data * 16 + 0x125] = 0x20;
        for (unsigned type = 1; type <= 8; ++type) {
            memory[directory * 16 + 2 * 6 + 0xba] = type;
            require(gateway::object_flag(read, ds, 2, 3), "valid object flag missing");
        }
        memory[data * 16 + 0x125] = 0;
        require(!gateway::object_flag(read, ds, 2, 3), "live flag update ignored");
        memory[data * 16 + 0x125] = 0x20;
        put(properties, 3 * 4 + 0x16, 1);
        require(!gateway::object_flag(read, ds, 2, 3), "incompatible property accepted");
        memory[directory * 16 + 2 * 6 + 0xba] = 1;
        require(gateway::object_flag(read, ds, 2, 3), "compatible property rejected");
        for (auto object : {0, 11, 0x8000, 0xffff})
            require(!gateway::object_flag(read, ds, object, 3), "invalid object accepted");
        require(!gateway::object_flag(read, ds, 2, 0), "zero property accepted");
        for (auto type : {0, 9, 255}) {
            memory[directory * 16 + 2 * 6 + 0xba] = type;
            require(!gateway::object_flag(read, ds, 2, 3), "invalid object type accepted");
        }
        // Offset arithmetic must wrap without carrying into the far segment.
        memory[directory * 16 + 2 * 6 + 0xba] = 1;
        put(directory, 2 * 6 + 0xbc, 0xfffc);
        memory[data * 16 + 1] = 0x20;
        require(gateway::object_flag(read, ds, 2, 3), "far offset wrap changed segment");
        std::cout << "Object flag layout checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
