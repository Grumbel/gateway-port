#include "rtlink.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void put(std::vector<std::uint8_t> &b, std::size_t at, std::uint16_t v) {
    b.at(at) = v & 255;
    b.at(at + 1) = v >> 8;
}
struct Fixture {
    std::vector<std::uint8_t> exe = std::vector<std::uint8_t>(0x1000);
    std::vector<std::uint8_t> ovl = std::vector<std::uint8_t>(0x40);
    Fixture() {
        put(exe, 0xc19, 3); // Two sections and an empty sentinel.
        const std::string name = "TEST.OVL";
        std::copy(name.begin(), name.end(), exe.begin() + 0xf01);
        for (unsigned id = 0; id < 2; ++id) {
            const auto p = 0xc1f + id * 18;
            put(exe, p, 0x2000 + id * 2);
            put(exe, p + 2, 0xf00);
            put(exe, p + 4, id * 2);
            exe[p + 7] = 4;
            put(exe, p + 8, 2); // One initialized paragraph plus one BSS paragraph.
            put(exe, p + 10, 1);
            put(exe, p + 12, id ? 0 : 0xffff);
            put(exe, p + 16, 1);
            put(ovl, id * 0x20, 3);
            put(ovl, id * 0x20 + 2, 0x2000 + id * 2);
            put(ovl, id * 0x20 + 0x13, 0xfff0);
        }
    }
};
void rejects(const Fixture &f) {
    bool rejected = false;
    try {
        gateway::parse_rtlink(f.exe, f.ovl, 0);
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    require(rejected, "malformed RTLink directory accepted");
}
int main() {
    try {
        Fixture f;
        auto directory = gateway::parse_rtlink(f.exe, f.ovl, 0);
        require(directory.sections.size() == 2, "sentinel counted as a section");
        const auto section = directory.sections[0];
        require(section.filename == "TEST.OVL" && section.image_file_offset == 0x10 &&
                    section.fixups[0].image_offset == 3,
                "section/fixup bounds wrong");
        const auto relocated = gateway::relocate_rtlink(section, 0x20);
        require(relocated[3] == 0x10 && relocated[4] == 0, "16-bit relocation did not wrap");
        std::vector<std::uint8_t> memory(0xa0000, 0x55);
        gateway::apply_rtlink(memory, section, 0x1000);
        require(memory[0x30003] == 0xf0 && memory[0x30004] == 0x0f, "relocation factor wrong");
        require(memory[0x30010] == 0 && memory[0x3001f] == 0 && memory[0x30020] == 0x55,
                "BSS zero fill crossed memory extent");
        // Omitted bytes in the last disk paragraph are left untouched, unlike BSS.
        f.ovl.resize(0x3b);
        directory = gateway::parse_rtlink(f.exe, f.ovl, 0);
        require(directory.sections[1].image.size() == 11, "final partial paragraph rejected");
        gateway::apply_rtlink(memory, directory.sections[1], 0x1000);
        require(memory[0x3002b] == 0x55 && memory[0x30030] == 0, "tail confused with BSS");
        // Errors must not partially mutate a destination.
        const auto before = memory;
        bool failed = false;
        try {
            gateway::apply_rtlink(memory, section, 0xffff);
        } catch (const std::out_of_range &) {
            failed = true;
        }
        require(failed && memory == before, "out-of-bounds load changed memory");
        f = Fixture();
        f.exe.resize(20);
        rejects(f);
        f = Fixture();
        put(f.exe, 0xc19, 1);
        rejects(f);
        f = Fixture();
        put(f.exe, 0xc19, 65535);
        rejects(f);
        f = Fixture();
        f.exe[0xc1f + 36] = 1;
        rejects(f);
        f = Fixture();
        f.exe[0xc1f + 7] = 0x24;
        rejects(f);
        f = Fixture();
        put(f.exe, 0xc1f + 2, 0xffff);
        rejects(f);
        f = Fixture();
        std::fill(f.exe.begin() + 0xf01, f.exe.end(), 'A');
        rejects(f);
        f = Fixture();
        put(f.exe, 0xc1f + 10, 1000);
        rejects(f);
        f = Fixture();
        put(f.ovl, 0, 15);
        rejects(f); // Word extends beyond file-backed image.
        f = Fixture();
        put(f.ovl, 2, 0x1fff);
        rejects(f);
        f = Fixture();
        put(f.exe, 0xc1f + 8, 0);
        rejects(f);
        f = Fixture();
        put(f.exe, 0xc1f + 12, 1);
        rejects(f);
        f = Fixture();
        put(f.exe, 0xc1f + 18, 0x2003);
        rejects(f); // Parent not adjacent.
        f = Fixture();
        put(f.exe, 0xc1f + 18 + 4, 3);
        rejects(f); // File gap.
        f = Fixture();
        f.ovl.resize(0x30);
        rejects(f);
        std::cout << "RTLink bounds, relocation, parent, BSS and partial-paragraph checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
