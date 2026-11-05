// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "picker_capture.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace gateway {

std::string write_picker_entry_capture(const std::filesystem::path &directory,
                                       const PickerEntryCapture &capture,
                                       std::span<const std::uint8_t> guest_ram) {
    if (!capture.call || capture.call > 9999 || guest_ram.size() != 0x100000)
        throw std::invalid_argument("picker capture needs a valid call and exactly 1 MiB RAM");
    if (!std::all_of(capture.picture.begin(), capture.picture.end(), [](unsigned char ch) {
            return (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                   ch == '.' || ch == '_' || ch == '-';
        }))
        throw std::invalid_argument("picker capture picture name is not canonical");
    std::ostringstream name;
    name << "call-" << std::setw(4) << std::setfill('0') << capture.call;
    const auto base = name.str();
    const auto memory_path = directory / (base + ".bin");
    const auto meta_path = directory / (base + ".meta");
    if (std::filesystem::exists(memory_path) || std::filesystem::exists(meta_path))
        throw std::runtime_error("picker capture would overwrite an existing file");

    // Prepare metadata first; the binary must still reflect the exact entry
    // state even when a later metadata write fails.
    std::ofstream metadata(meta_path);
    if (!metadata)
        throw std::runtime_error("cannot create picker capture metadata");
    metadata << "format=gateway-picker-entry-v1\n"
             << "call=" << capture.call << "\n"
             << "guest_ms=" << capture.guest_ms << "\n"
             << "x=" << capture.x << "\n"
             << "y=" << capture.y << "\n"
             << "return_cs=0x" << std::hex << capture.return_cs << "\n"
             << "return_ip=0x" << capture.return_ip << "\n"
             << "picture=" << capture.picture << "\n"
             << "picture_entry=" << std::dec << capture.picture_entry << "\n";
    constexpr const char *register_names[] = {
        "AX", "BX", "CX", "DX", "CS", "IP", "SS", "SP", "DS", "ES", "BP", "FLAGS"};
    for (std::size_t i = 0; i < capture.registers.size(); ++i)
        metadata << register_names[i] << "=0x" << std::hex << capture.registers[i] << "\n";
    metadata.close();
    if (!metadata)
        throw std::runtime_error("cannot write picker capture metadata");

    std::ofstream binary(memory_path, std::ios::binary);
    if (!binary)
        throw std::runtime_error("cannot create picker capture RAM");
    binary.write(reinterpret_cast<const char *>(guest_ram.data()),
                 std::streamsize(guest_ram.size()));
    binary.close();
    if (!binary)
        throw std::runtime_error("cannot write picker capture RAM");
    return base;
}

void write_picker_result_capture(const std::filesystem::path &directory,
                                 std::string_view basename, std::string_view result_trace) {
    if (basename.size() != 9 || !basename.starts_with("call-") ||
        !std::all_of(basename.begin() + 5, basename.end(), [](unsigned char ch) {
            return ch >= '0' && ch <= '9';
        }) || result_trace.empty())
        throw std::invalid_argument("invalid picker return capture");
    const auto output_path = directory / (std::string(basename) + ".result");
    if (std::filesystem::exists(output_path))
        throw std::runtime_error("picker return capture would overwrite an existing file");
    std::ofstream output(output_path);
    if (!output)
        throw std::runtime_error("cannot create picker return capture");
    output << result_trace;
    output.close();
    if (!output)
        throw std::runtime_error("cannot write picker return capture");
}

} // namespace gateway
