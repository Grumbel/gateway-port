// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

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
        const gateway::GuestByteReader read = [&](std::uint16_t segment, std::uint32_t offset) {
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
        const auto apply = [&](const std::optional<gateway::GuestWrite> &write) {
            require(write.has_value(), "valid write rejected");
            for (unsigned i = 0; i < write->size; ++i)
                memory.at(write->segment * 16 + write->offset + i) = write->bytes[i];
        };
        // Set/clear must preserve all unrelated flag bits, including repeated writes.
        memory[data * 16 + 1] = 0x85;
        apply(gateway::object_flag_write(read, ds, 2, 3, true));
        require(memory[data * 16 + 1] == 0xa5, "flag set damaged neighboring bits");
        apply(gateway::object_flag_write(read, ds, 2, 3, true));
        apply(gateway::object_flag_write(read, ds, 2, 3, false));
        require(memory[data * 16 + 1] == 0x85, "flag clear damaged neighboring bits");
        require(!gateway::object_flag_write(read, ds, 0, 3, true), "invalid object write accepted");
        require(!gateway::object_flag_write(read, ds, 2, 0, true),
                "invalid property write accepted");
        put(properties, 3 * 4 + 0x16, 0);
        require(!gateway::object_flag_write(read, ds, 2, 3, true),
                "masked property write accepted");
        // Location fields vary by type. Full 16-bit values must survive writes and reads.
        put(directory, 2 * 6 + 0xbc, 0x120);
        for (unsigned type = 1; type <= 8; ++type) {
            memory[directory * 16 + 2 * 6 + 0xba] = type;
            const bool supported = type == 2 || type == 6 || type == 7 || type == 8;
            const auto write = gateway::object_location_write(read, ds, 2, 0x8123);
            require(bool(write) == supported, "location type applicability incorrect");
            if (supported) {
                const unsigned offset = type == 2 ? 0x0e : type == 6 ? 0x0a : 0x0c;
                require(write->offset == 0x120 + offset && write->size == 2,
                        "wrong location field");
                apply(write);
                require(gateway::object_location(read, ds, 2) == 0x8123,
                        "location write truncated");
            } else
                require(gateway::object_location(read, ds, 2) == 0, "unsupported location read");
        }
        require(!gateway::object_location_write(read, ds, 11, 1),
                "invalid location object accepted");
        memory[directory * 16 + 2 * 6 + 0xba] = 2;
        put(directory, 2 * 6 + 0xbc,
            0xfff1); // Location low byte at ffff, high byte at the next linear byte.
        const auto wrapped = gateway::object_location_write(read, ds, 2, 0xabcd);
        apply(wrapped);
        require(memory[data * 16 + 0xffff] == 0xcd && memory[data * 16 + 0x10000] == 0xab,
                "location word write did not use contiguous bytes");
        require(gateway::object_location(read, ds, 2) == 0xabcd,
                "location word read did not use contiguous bytes");
        std::cout << "Object flag and location layout checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
