// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "mz.hpp"
#include <iostream>
#include <stdexcept>

void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void put(std::vector<std::uint8_t> &b, std::size_t at, std::uint16_t value) {
    b[at] = value & 255;
    b[at + 1] = value >> 8;
}
std::vector<std::uint8_t> fixture() {
    std::vector<std::uint8_t> b(64);
    put(b, 0, 0x5a4d);
    put(b, 2, 64);
    put(b, 4, 1);
    put(b, 6, 1);
    put(b, 8, 2);
    put(b, 10, 2);
    put(b, 16, 64);
    put(b, 24, 28);
    put(b, 28, 2);
    put(b, 34, 0x1234);
    return b;
}
void rejects(const std::vector<std::uint8_t> &b) {
    try {
        gateway::load_mz(b);
    } catch (const std::runtime_error &) {
        return;
    }
    throw std::runtime_error("malformed image accepted");
}
int main() {
    try {
        auto b = fixture();
        auto image = gateway::load_mz(b);
        require(image.cs == 0x1000 && image.ip == 0 && image.ss == 0x1000 && image.sp == 64,
                "incorrect startup registers");
        require(image.ds == 0x0ff0 && image.memory[0xff00] == 0xcd, "PSP not initialized");
        require(image.memory[0x10002] == 0x34 && image.memory[0x10003] == 0x22,
                "segment relocation not applied");
        require(image.memory[0x10020] == 0, "allocation not zero initialized");
        const auto overlay = gateway::load_mz_overlay(b, 0x2000, 0x1111);
        require(overlay.size() == 32 && overlay[2] == 0x45 && overlay[3] == 0x23,
                "overlay load address incorrectly used as relocation factor");
        auto overlay_header = b;
        put(overlay_header, 10, 65535);
        put(overlay_header, 14, 65535);
        put(overlay_header, 20, 65535);
        require(gateway::load_mz_overlay(overlay_header, 0x2000, 0).size() == 32,
                "overlay imposed process allocation/entry requirements");
        try {
            gateway::load_mz_overlay(b, 0x9fff, 0);
            throw std::runtime_error("overlay overflow accepted");
        } catch (const std::out_of_range &) {
        }
        auto bad_overlay = b;
        put(bad_overlay, 28, 31);
        bool rejected = false;
        try {
            gateway::load_mz_overlay(bad_overlay, 0x1000, 0);
        } catch (const std::runtime_error &) {
            rejected = true;
        }
        require(rejected, "overlay relocation outside image accepted");
        rejects({});
        b = fixture();
        b.resize(40);
        rejects(b);
        b = fixture();
        put(b, 2, 512);
        rejects(b);
        b = fixture();
        put(b, 8, 1);
        rejects(b);
        b = fixture();
        put(b, 24, 31);
        rejects(b);
        b = fixture();
        put(b, 28, 31);
        rejects(b);
        b = fixture();
        put(b, 20, 32);
        rejects(b);
        b = fixture();
        put(b, 16, 65);
        rejects(b);
        b = fixture();
        put(b, 10, 65535);
        rejects(b);
        b = fixture();
        put(b, 26, 1);
        rejects(b);
        b = fixture();
        put(b, 6, 0);
        b.resize(512);
        put(b, 2, 0);
        require(gateway::load_mz(b).image_size == 480, "full last page mishandled");
        std::cout << "MZ loader checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
