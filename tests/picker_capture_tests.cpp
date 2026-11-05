// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "picker_capture.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace

int main() {
    const auto directory = std::filesystem::temp_directory_path() /
        ("gateway-picker-capture-tests-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    require(std::filesystem::create_directory(directory), "cannot create test directory");
    gateway::PickerEntryCapture capture;
    capture.call = 1;
    capture.guest_ms = 123;
    capture.x = 360;
    capture.y = 200;
    capture.return_cs = 0x1400;
    capture.return_ip = 0x33;
    capture.picture = "GATE_102.PIC";
    capture.picture_entry = 15;
    capture.registers[8] = 0x3123; // DS
    std::vector<std::uint8_t> memory(0x100000, 0x67);
    require(gateway::write_picker_entry_capture(directory, capture, memory) == "call-0001",
            "unexpected capture basename");
    const auto ram = directory / "call-0001.bin";
    const auto metadata = directory / "call-0001.meta";
    require(std::filesystem::file_size(ram) == memory.size(), "invalid RAM size");
    std::ifstream input(metadata);
    const std::string content{std::istreambuf_iterator<char>(input), {}};
    require(content.find("format=gateway-picker-entry-v1\n") != std::string::npos,
            "missing capture format");
    require(content.find("picture=GATE_102.PIC\n") != std::string::npos,
            "missing picture identity");
    require(content.find("picture_entry=15\n") != std::string::npos,
            "missing picture frame");
    require(content.find("DS=0x3123\n") != std::string::npos,
            "missing register snapshot");
    bool refused = false;
    try {
        gateway::write_picker_entry_capture(directory, capture, memory);
    } catch (const std::runtime_error &) {
        refused = true;
    }
    require(refused, "capture overwrote existing RAM");
    refused = false;
    capture.call = 2;
    try {
        gateway::write_picker_entry_capture(directory, capture, std::span(memory).first(4));
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    require(refused, "capture accepted incomplete RAM");
    require(!std::filesystem::exists(directory / "call-0002.meta"),
            "bad capture left metadata behind");
    std::filesystem::remove(ram);
    std::filesystem::remove(metadata);
    std::filesystem::remove(directory);
}
