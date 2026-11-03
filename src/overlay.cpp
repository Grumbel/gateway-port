// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "rtlink.hpp"
#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace {
std::vector<std::uint8_t> read(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("cannot open " + path.string());
    return {std::istreambuf_iterator<char>(input), {}};
}
void write(const std::filesystem::path &path, const std::vector<std::uint8_t> &bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    output.close();
    if (!output)
        throw std::runtime_error("cannot write " + path.string());
}
unsigned hex(const std::string &text) {
    unsigned value;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        throw std::runtime_error("expected a hexadecimal number");
    return value;
}
std::uint16_t word(const std::vector<std::uint8_t> &bytes, std::size_t at) {
    if (at >= bytes.size() || bytes.size() - at < 2)
        throw std::runtime_error("truncated snapshot word");
    return bytes[at] | (std::uint16_t(bytes[at + 1]) << 8);
}
} // namespace
int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr
            << "Usage: gateway-overlay EXE OVL [--output DIR] [--snapshot PREFIX] "
               "[--relocation HEX] [--manager-offset HEX] [--manager-segment HEX] [--fixups]\n";
        return 2;
    }
    try {
        std::filesystem::path output, snapshot;
        unsigned factor = 0x1000, manager_segment = 0x1c24, manager_offset = 0xa020;
        bool fixups = false;
        for (int i = 3; i < argc; ++i) {
            const std::string option = argv[i];
            if (option == "--fixups")
                fixups = true;
            else if (i + 1 == argc)
                throw std::runtime_error("missing overlay option value");
            else if (option == "--output")
                output = argv[++i];
            else if (option == "--snapshot")
                snapshot = argv[++i];
            else if (option == "--relocation")
                factor = hex(argv[++i]);
            else if (option == "--manager-offset")
                manager_offset = hex(argv[++i]);
            else if (option == "--manager-segment")
                manager_segment = hex(argv[++i]);
            else
                throw std::runtime_error("unknown overlay option");
        }
        if (factor > 0xffff || manager_segment > 0xffff || manager_segment + factor > 0xffff)
            throw std::runtime_error("overlay relocation/manager segment out of range");
        const auto exe = read(argv[1]), ovl = read(argv[2]);
        const auto directory = gateway::parse_rtlink(exe, ovl, manager_offset);
        const auto memory =
            snapshot.empty() ? std::vector<std::uint8_t>{} : read(snapshot.string() + ".bin");
        const auto manager_base = std::size_t(manager_segment + factor) * 16;
        if (!memory.empty() && word(memory, manager_base + 0xc19) != directory.sections.size() + 1)
            throw std::runtime_error("snapshot has a different section count");
        if (!output.empty())
            std::filesystem::create_directories(output);
        unsigned loaded_count = 0, mismatch_count = 0;
        std::size_t relocation_count = 0;
        std::cout << "{\"manager_file_offset\":" << manager_offset << ",\"relocation\":" << factor
                  << ",\"manager_linked_segment\":" << manager_segment << ",\"sections\":[\n";
        for (const auto &section : directory.sections) {
            const auto relocated = gateway::relocate_rtlink(section, factor);
            const auto runtime_segment = std::size_t(section.linked_segment) + factor;
            if (runtime_segment > 0xffff || runtime_segment * 16 + section.memory_size() > 0xa0000)
                throw std::runtime_error("relocated section exceeds conventional memory");
            relocation_count += section.fixups.size();
            if (section.id)
                std::cout << ",\n";
            std::cout << "{\"id\":" << section.id
                      << ",\"filename\":" << std::quoted(section.filename)
                      << ",\"linked_segment\":" << section.linked_segment
                      << ",\"runtime_segment\":" << runtime_segment
                      << ",\"relocation_file_offset\":" << section.relocation_file_offset
                      << ",\"relocation_count\":" << section.fixups.size()
                      << ",\"image_file_offset\":" << section.image_file_offset
                      << ",\"file_bytes\":" << section.image.size()
                      << ",\"initialized_size\":" << section.initialized_size()
                      << ",\"memory_size\":" << section.memory_size()
                      << ",\"missing_paragraph_tail\":"
                      << section.initialized_size() - section.image.size()
                      << ",\"unknown_word\":" << section.unknown_word << ",\"parent\":";
            if (section.parent == 0xffff)
                std::cout << "null";
            else
                std::cout << section.parent;
            std::cout << ",\"overlaps\":[";
            bool first = true;
            for (const auto &other : directory.sections)
                if (section.id != other.id &&
                    section.linked_segment <
                        std::size_t(other.linked_segment) + other.memory_paragraphs &&
                    other.linked_segment <
                        std::size_t(section.linked_segment) + section.memory_paragraphs) {
                    if (!first)
                        std::cout << ',';
                    std::cout << other.id;
                    first = false;
                }
            std::cout << ']';
            if (fixups) {
                std::cout << ",\"fixups\":[";
                for (std::size_t i = 0; i < section.fixups.size(); ++i) {
                    if (i)
                        std::cout << ',';
                    const auto &fixup = section.fixups[i];
                    std::cout << "{\"offset\":" << fixup.offset << ",\"segment\":" << fixup.segment
                              << ",\"image_offset\":" << fixup.image_offset << '}';
                }
                std::cout << ']';
            }
            if (!memory.empty()) {
                const auto record = manager_base + 0xc1f + section.id * 18;
                if (word(memory, record) != runtime_segment)
                    throw std::runtime_error("snapshot section segment differs from directory");
                const bool loaded = (word(memory, record + 6) >> 8) & 2;
                std::cout << ",\"snapshot_loaded\":" << (loaded ? "true" : "false");
                if (loaded) {
                    ++loaded_count;
                    const auto base = runtime_segment * 16;
                    if (base + section.memory_size() > memory.size())
                        throw std::runtime_error("snapshot shorter than loaded section");
                    std::size_t differences = 0;
                    for (std::size_t i = 0; i < relocated.size(); ++i)
                        differences += relocated[i] != memory[base + i];
                    for (auto i = section.initialized_size(); i < section.memory_size(); ++i)
                        differences += memory[base + i] != 0;
                    mismatch_count += differences != 0;
                    std::cout << ",\"snapshot_differences\":" << differences;
                }
            }
            std::cout << '}';
            if (!output.empty()) {
                std::ostringstream name;
                name << "section-" << std::setfill('0') << std::setw(3) << section.id;
                write(output / (name.str() + ".raw.bin"), section.image);
                write(output / (name.str() + ".relocated.bin"), relocated);
            }
        }
        std::cout << "\n],\"section_count\":" << directory.sections.size()
                  << ",\"relocation_count\":" << relocation_count
                  << ",\"snapshot_loaded_count\":" << loaded_count
                  << ",\"snapshot_mismatched_sections\":" << mismatch_count << "}\n";
        if (!std::cout)
            throw std::runtime_error("cannot write overlay manifest");
        return mismatch_count ? 1 : 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
