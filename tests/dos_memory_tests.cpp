#include "dos_memory.hpp"
#include <iostream>
#include <stdexcept>

void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
int main() {
    try {
        gateway::DosMemory process(0x1000, 0x1100, 0x0fe0);
        require(process.blocks().front().paragraphs == 31 && process.release(0x0fe0).error == 0 &&
                    process.allocate(16).segment == 0x0fe0,
                "environment block not tracked/reusable");
        gateway::DosMemory arena(0x1000, 0x1100);
        require(arena.allocate(1).error == 8, "initial block must occupy arena");
        require(arena.resize(0x1234, 1).error == 9, "invalid resize accepted");
        require(arena.resize(0x1000, 0x20).error == 0, "PSP shrink failed");
        auto a = arena.allocate(0x30);
        auto b = arena.allocate(0x10);
        require(a.segment == 0x1021 && b.segment == 0x1052, "MCB paragraph not reserved");
        require(arena.resize(a.segment, 0x40).error == 8, "growth crossed live neighbor");
        require(arena.release(b.segment).error == 0, "free failed");
        require(arena.release(b.segment).error == 9, "double free accepted");
        auto growth = arena.resize(a.segment, 0xffff);
        require(growth.error == 8 && growth.available == 0xdf, "largest resize not reported");
        require(arena.blocks().back().paragraphs == 0xdf, "failed resize did not grow");
        require(arena.resize(a.segment, 0x30).error == 0, "shrink failed");
        require(arena.release(a.segment).error == 0, "free failed");
        auto all = arena.allocate(0xdf);
        require(all.segment == 0x1021 && arena.blocks().size() == 2, "coalescing failed");
        require(arena.release(all.segment).error == 0, "free failed");
        auto zero = arena.allocate(0);
        require(zero.error == 0 && zero.segment == 0x1021, "zero-size allocation failed");
        std::vector<std::uint8_t> memory(1024 * 1024);
        arena.write_mcbs(memory);
        require(memory[0xfff0] == 'M' && memory[0xfff3] == 0x20,
                "first MCB type or size incorrect");
        const auto last = std::size_t(arena.blocks().back().segment - 1) * 16;
        require(memory[last] == 'Z' && memory[last + 1] == 0,
                "last MCB must be a free terminal block");
        std::cout << "DOS arena checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
