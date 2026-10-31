#include "dos_clock.hpp"
#include "dos_fcb.hpp"
#include "dos_files.hpp"
#include "dos_memory.hpp"
#include "mz.hpp"
#include "opl.hpp"
#include "pit.hpp"
#include "vga.hpp"
#ifdef GATEWAY_SDL
#include "sdl_host.hpp"
#endif
#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unicorn/unicorn.h>

namespace {
void check(uc_err error) {
    if (error != UC_ERR_OK)
        throw std::runtime_error(uc_strerror(error));
}
struct Engine {
    uc_engine *uc = nullptr;
    Engine() { check(uc_open(UC_ARCH_X86, UC_MODE_16, &uc)); }
    ~Engine() { uc_close(uc); }
};
std::uint16_t reg(uc_engine *uc, int id) {
    std::uint16_t value = 0;
    check(uc_reg_read(uc, id, &value));
    return value;
}
void set(uc_engine *uc, int id, std::uint16_t value) { check(uc_reg_write(uc, id, &value)); }
struct State {
    std::filesystem::path data_dir;
    std::unique_ptr<gateway::DosDrive> drive;
    std::map<unsigned, gateway::DosFile> files;
    struct Search {
        std::vector<gateway::DosDirectoryEntry> entries;
        std::size_t next = 0;
    };
    std::map<std::uint32_t, Search> searches;
    std::uint32_t next_search = 1;
    std::array<bool, 5> standard_open{true, true, true, true, true};
    gateway::DosMemory arena{0x0ff0, 0xa000, 0x0fd0};
    gateway::Vga vga;
    gateway::Opl opl;
    gateway::Pit pit;
    gateway::DosClock clock;
    std::array<std::uint16_t, 8> cursor{};
    std::array<std::array<std::uint8_t, 8>, 256> bios_font{};
    bool font_loaded = false;
    std::uint16_t cursor_shape = 0x0607;
    std::uint16_t dta_segment = 0x0ff0, dta_offset = 0x80;
    std::uint8_t pending_dos_scan = 0;
    std::deque<std::uint16_t> keys;
    std::set<unsigned> polled_services;
    std::uint8_t pic_mask = 0;
    bool irq_in_service = false;
    bool breakpoint_hit = false;
    bool exited = false;
    unsigned exit_code = 0;
    std::string failure;
};
std::string guest_filename(uc_engine *uc) {
    std::string name;
    const auto address = std::size_t(reg(uc, UC_X86_REG_DS)) * 16 + reg(uc, UC_X86_REG_DX);
    for (unsigned i = 0; i < 260; ++i) {
        char c;
        check(uc_mem_read(uc, address + i, &c, 1));
        if (!c)
            return name;
        name += c;
    }
    throw std::runtime_error("unterminated DOS filename");
}
unsigned free_file_handle(const State &state) {
    for (unsigned handle = 0; handle < 20; ++handle)
        if (!(handle < state.standard_open.size() && state.standard_open[handle]) &&
            !state.files.contains(handle))
            return handle;
    throw gateway::DosIoError(4, "DOS handle table is full");
}

void hardware_failure(uc_engine *uc, State &state, const std::string &message) {
    state.failure = message;
    uc_emu_stop(uc);
}
std::uint32_t port_in(uc_engine *uc, std::uint32_t port, int size, void *opaque) noexcept {
    auto &state = *static_cast<State *>(opaque);
    try {
        std::uint32_t value = 0;
        for (int i = 0; i < size; ++i) {
            if (state.pit.handles_port(port + i)) {
                value |= std::uint32_t(state.pit.in(port + i)) << (8 * i);
                continue;
            }
            if (port + i == 0x21) {
                value |= std::uint32_t(state.pic_mask) << (8 * i);
                continue;
            }
            if (state.opl.handles_port(port + i)) {
                value |= std::uint32_t(state.opl.in(port + i)) << (8 * i);
                continue;
            }
            if (!state.vga.handles_port(port + i)) {
                std::ostringstream message;
                message << "unsupported hardware port read: " << std::hex << port + i;
                throw std::runtime_error(message.str());
            }
            value |= std::uint32_t(state.vga.in(port + i)) << (8 * i);
        }
        return value;
    } catch (const std::exception &e) {
        hardware_failure(uc, state, e.what());
        return 0;
    }
}
void port_out(uc_engine *uc, std::uint32_t port, int size, std::uint32_t value,
              void *opaque) noexcept {
    auto &state = *static_cast<State *>(opaque);
    try {
        for (int i = 0; i < size; ++i) {
            const std::uint8_t byte = (value >> (8 * i)) & 255;
            if (state.pit.handles_port(port + i)) {
                state.pit.out(port + i, byte);
                continue;
            }
            if (port + i == 0x21) {
                state.pic_mask = byte;
                continue;
            }
            if (port + i == 0x20 && byte == 0x20) {
                state.irq_in_service = false;
                continue;
            }
            if (state.opl.handles_port(port + i)) {
                state.opl.out(port + i, (value >> (8 * i)) & 255);
                continue;
            }
            if (!state.vga.handles_port(port + i)) {
                std::ostringstream message;
                message << "unsupported hardware port write: " << std::hex << port + i;
                throw std::runtime_error(message.str());
            }
            state.vga.out(port + i, (value >> (8 * i)) & 255);
        }
    } catch (const std::exception &e) {
        hardware_failure(uc, state, e.what());
    }
}
void video_memory(uc_engine *uc, uc_mem_type type, std::uint64_t address, int size,
                  std::int64_t value, void *opaque) noexcept {
    auto &state = *static_cast<State *>(opaque);
    if (state.vga.mode() == 3)
        return;
    try {
        if (size > 8)
            throw std::runtime_error("unsupported VGA access width");
        std::array<std::uint8_t, 8> data{};
        for (int i = 0; i < size; ++i) {
            const auto offset = static_cast<std::uint16_t>(address - 0xa0000 + i);
            if (type == UC_MEM_READ)
                data[i] = state.vga.read(offset);
            else
                state.vga.write(offset, (static_cast<std::uint64_t>(value) >> (8 * i)) & 255);
        }
        if (type == UC_MEM_READ)
            check(uc_mem_write(uc, address, data.data(), size));
    } catch (const std::exception &e) {
        hardware_failure(uc, state, e.what());
    }
}

void text_mode_state(uc_engine *uc, State &state, bool clear) {
    state.cursor.fill(0);
    state.cursor_shape = 0x0607;
    const std::uint16_t columns = 80, page_size = 4096, start = 0, shape = 0x0607, height = 16;
    const std::uint8_t mode = 3, page = 0, rows = 24;
    check(uc_mem_write(uc, 0x449, &mode, 1));
    check(uc_mem_write(uc, 0x44a, &columns, 2));
    check(uc_mem_write(uc, 0x44c, &page_size, 2));
    check(uc_mem_write(uc, 0x44e, &start, 2));
    check(uc_mem_write(uc, 0x450, state.cursor.data(), sizeof(state.cursor)));
    check(uc_mem_write(uc, 0x460, &shape, 2));
    check(uc_mem_write(uc, 0x462, &page, 1));
    check(uc_mem_write(uc, 0x484, &rows, 1));
    check(uc_mem_write(uc, 0x485, &height, 2));
    if (clear)
        check(uc_mem_write(uc, 0xb8000, state.vga.text_memory().data(),
                           state.vga.text_memory().size()));
}
void text_snapshot(uc_engine *uc, State &state) {
    if (state.vga.mode() != 3)
        return;
    std::array<std::uint8_t, 32768> memory;
    check(uc_mem_read(uc, 0xb8000, memory.data(), memory.size()));
    state.vga.text_snapshot(memory);
}

bool dispatch(uc_engine *uc, unsigned number) {
    std::uint16_t vector[2]{};
    check(uc_mem_read(uc, number * 4, vector, sizeof(vector)));
    if (!vector[0] && !vector[1])
        return false;
    auto sp = reg(uc, UC_X86_REG_SP);
    const auto base = std::size_t(reg(uc, UC_X86_REG_SS)) * 16;
    for (auto value : {reg(uc, UC_X86_REG_FLAGS), reg(uc, UC_X86_REG_CS), reg(uc, UC_X86_REG_IP)}) {
        sp -= 2;
        check(uc_mem_write(uc, base + sp, &value, 2));
    }
    set(uc, UC_X86_REG_SP, sp);
    set(uc, UC_X86_REG_FLAGS, reg(uc, UC_X86_REG_FLAGS) & ~0x300);
    set(uc, UC_X86_REG_CS, vector[1]);
    set(uc, UC_X86_REG_IP, vector[0]);
    return true;
}

void interrupt(uc_engine *uc, std::uint32_t number, void *opaque) noexcept {
    auto &state = *static_cast<State *>(opaque);
    try {
        auto ax = reg(uc, UC_X86_REG_AX);
        const bool frequent = number == 0x16 || number == 0xf1 || number == 0x1c ||
                              (number == 0x21 && ((ax >> 8) == 0x2a || (ax >> 8) == 0x2c));
        const bool first =
            !frequent || state.polled_services.insert((number << 8) | (ax >> 8)).second;
        if (first)
            std::cerr << "INT " << std::hex << number << " AX=" << std::setw(4) << std::setfill('0')
                      << ax << " BX=" << reg(uc, UC_X86_REG_BX) << " CX=" << reg(uc, UC_X86_REG_CX)
                      << " DX=" << reg(uc, UC_X86_REG_DX) << " at " << reg(uc, UC_X86_REG_CS) << ':'
                      << reg(uc, UC_X86_REG_IP) << '\n';
        if (number == 0x21 && ((ax >> 8) == 7 || (ax >> 8) == 8 || (ax >> 8) == 1)) {
            if (state.pending_dos_scan) {
                set(uc, UC_X86_REG_AX, (ax & 0xff00) | state.pending_dos_scan);
                state.pending_dos_scan = 0;
            } else if (state.keys.empty()) {
                set(uc, UC_X86_REG_IP, reg(uc, UC_X86_REG_IP) - 2);
                check(uc_emu_stop(uc));
            } else {
                const auto key = state.keys.front();
                state.keys.pop_front();
                const auto ascii = key & 255;
                if (!ascii)
                    state.pending_dos_scan = key >> 8;
                set(uc, UC_X86_REG_AX, (ax & 0xff00) | ascii);
                if ((ax >> 8) == 1 && ascii)
                    std::cout.put(ascii);
            }
        } else if (number == 0x16 && ((ax >> 8) == 1 || (ax >> 8) == 0x11)) {
            const bool empty = state.keys.empty();
            set(uc, UC_X86_REG_FLAGS, (reg(uc, UC_X86_REG_FLAGS) & ~0x40) | (empty ? 0x40 : 0));
            if (!empty)
                set(uc, UC_X86_REG_AX, state.keys.front());
        } else if (number == 0x16 && ((ax >> 8) == 0 || (ax >> 8) == 0x10)) {
            if (state.keys.empty()) {
                set(uc, UC_X86_REG_IP, reg(uc, UC_X86_REG_IP) - 2);
                check(uc_emu_stop(uc));
            } else {
                set(uc, UC_X86_REG_AX, state.keys.front());
                state.keys.pop_front();
            }
        } else if (number == 0x16 && (ax >> 8) == 2) {
            set(uc, UC_X86_REG_AX, ax & 0xff00);
        } else if (number == 0xf1 && reg(uc, UC_X86_REG_CS) == 0xf000) {
            std::uint32_t ticks;
            check(uc_mem_read(uc, 0x46c, &ticks, 4));
            ticks = (ticks + 1) % 0x1800b0;
            check(uc_mem_write(uc, 0x46c, &ticks, 4));
        } else if (number == 0x10 && ax == 0x1a00) {
            set(uc, UC_X86_REG_AX, 0x1a);
            set(uc, UC_X86_REG_BX, 8); // VGA with an analog color display.
        } else if (number == 0x10 && (ax >> 8) == 9) {
            const auto bx = reg(uc, UC_X86_REG_BX);
            const auto page = (bx >> 8) & 7;
            const auto position = state.cursor[page];
            const unsigned row = position >> 8, column = position & 255;
            const auto character = ax & 255;
            if (state.vga.mode() == 3) {
                const std::uint8_t cell[]{static_cast<std::uint8_t>(character),
                                          static_cast<std::uint8_t>(bx)};
                const auto first = row * 80 + column;
                for (unsigned i = 0; i < reg(uc, UC_X86_REG_CX) && first + i < 2000; ++i)
                    check(uc_mem_write(uc, 0xb8000 + page * 4096 + (first + i) * 2, cell, 2));
            } else {
                const unsigned height = state.vga.mode() == 0x12   ? 16
                                        : state.vga.mode() == 0x10 ? 14
                                                                   : 8;
                if (!state.font_loaded && character != ' ')
                    throw std::runtime_error("BIOS glyph needs game font GATE_000.FNT");
                std::array<std::uint8_t, 16> glyph{};
                for (unsigned i = 0; i < height; ++i)
                    glyph[i] = state.bios_font[character][i * 8 / height];
                for (unsigned i = 0; i < reg(uc, UC_X86_REG_CX); ++i)
                    state.vga.draw_glyph(column + i, row, std::span(glyph).first(height), bx & 255);
            }
        } else if (number == 0x10 && (ax >> 8) == 8 && state.vga.mode() == 3) {
            const auto page = (reg(uc, UC_X86_REG_BX) >> 8) & 7;
            const auto position = state.cursor[page];
            const auto cell = (position >> 8) * 80 + (position & 255);
            std::uint16_t value = 0;
            if (cell < 2000)
                check(uc_mem_read(uc, 0xb8000 + page * 4096 + cell * 2, &value, 2));
            set(uc, UC_X86_REG_AX, value);
        } else if (number == 0x10 && (ax >> 8) == 5 && state.vga.mode() == 3) {
            const std::uint8_t page = ax & 7;
            state.vga.text_page(page);
            const std::uint16_t start = page * 4096;
            check(uc_mem_write(uc, 0x44e, &start, 2));
            check(uc_mem_write(uc, 0x462, &page, 1));
        } else if (number == 0x10 && ((ax >> 8) == 2 || (ax >> 8) == 3)) {
            const auto page = (reg(uc, UC_X86_REG_BX) >> 8) & 7;
            if ((ax >> 8) == 2) {
                state.cursor[page] = reg(uc, UC_X86_REG_DX);
                check(uc_mem_write(uc, 0x450 + page * 2, &state.cursor[page], 2));
            } else {
                set(uc, UC_X86_REG_DX, state.cursor[page]);
                set(uc, UC_X86_REG_CX, state.cursor_shape);
            }
        } else if (number == 0x10 && (ax >> 8) == 1) {
            state.cursor_shape = reg(uc, UC_X86_REG_CX);
        } else if (number == 0x10 && (ax >> 8) == 0x0f) {
            set(uc, UC_X86_REG_AX, 0x5000 | state.vga.mode());
            set(uc, UC_X86_REG_BX,
                (reg(uc, UC_X86_REG_BX) & 255) |
                    (state.vga.mode() == 3 ? state.vga.text_page() << 8 : 0));
        } else if (number == 0x10 && (ax >> 8) == 0) {
            const auto mode = ax & 0x7f;
            if (mode != 3 && mode != 0x0d && mode != 0x0e && mode != 0x10 && mode != 0x12 &&
                mode != 0x13)
                throw std::runtime_error("unsupported VGA mode");
            state.vga.set_mode(mode, !(ax & 0x80));
            if (mode == 3)
                text_mode_state(uc, state, !(ax & 0x80));
            const std::uint8_t value = mode;
            check(uc_mem_write(uc, 0x449, &value, 1));
        } else if (number == 0x20 || (number == 0x21 && (ax >> 8) == 0x4c)) {
            state.exited = true;
            state.exit_code = number == 0x20 ? 0 : ax & 255;
            check(uc_emu_stop(uc));
        } else if (number == 0x21 && ((ax >> 8) == 0x4e || (ax >> 8) == 0x4f)) {
            const auto dta = std::size_t(state.dta_segment) * 16 + state.dta_offset;
            std::array<std::uint8_t, 43> block{};
            std::uint32_t id = 0;
            if ((ax >> 8) == 0x4e) {
                const auto pattern = guest_filename(uc);
                std::cerr << "  find: " << pattern << '\n';
                auto entries = state.drive->list(pattern, reg(uc, UC_X86_REG_CX));
                check(uc_mem_write(uc, dta, block.data(), block.size()));
                if (entries.empty())
                    throw gateway::DosIoError(18, "no matching DOS files");
                id = state.next_search++;
                state.searches.emplace(id, State::Search{std::move(entries), 0});
            } else {
                check(uc_mem_read(uc, dta, block.data(), block.size()));
                if (!std::equal(block.begin(), block.begin() + 4, "GWFS"))
                    throw gateway::DosIoError(18, "no active DOS search");
                for (unsigned i = 0; i < 4; ++i)
                    id |= std::uint32_t(block[4 + i]) << (8 * i);
            }
            auto search = state.searches.find(id);
            if (search == state.searches.end() ||
                search->second.next >= search->second.entries.size()) {
                state.searches.erase(id);
                throw gateway::DosIoError(18, "no more DOS files");
            }
            const auto &entry = search->second.entries[search->second.next++];
            block.fill(0);
            std::copy_n("GWFS", 4, block.begin());
            for (unsigned i = 0; i < 4; ++i) {
                block[4 + i] = id >> (8 * i);
                block[26 + i] = entry.size >> (8 * i);
            }
            block[21] = entry.attributes;
            block[22] = entry.time & 255;
            block[23] = entry.time >> 8;
            block[24] = entry.date & 255;
            block[25] = entry.date >> 8;
            std::copy(entry.name.begin(), entry.name.end(), block.begin() + 30);
            check(uc_mem_write(uc, dta, block.data(), block.size()));
            set(uc, UC_X86_REG_AX, 0);
            set(uc, UC_X86_REG_FLAGS, reg(uc, UC_X86_REG_FLAGS) & ~1);
        } else if (number == 0x21 && ax == 0x4b03) {
            const auto name = guest_filename(uc);
            const auto parameters =
                std::size_t(reg(uc, UC_X86_REG_ES)) * 16 + reg(uc, UC_X86_REG_BX);
            std::array<std::uint8_t, 4> block{};
            check(uc_mem_read(uc, parameters, block.data(), block.size()));
            const std::uint16_t load_segment = block[0] | (std::uint16_t(block[1]) << 8);
            const std::uint16_t relocation = block[2] | (std::uint16_t(block[3]) << 8);
            std::cerr << "  overlay: " << name << " load=" << std::hex << load_segment
                      << " relocation=" << relocation << '\n';
            unsigned error = 0;
            const auto bytes = state.drive->read_all(name);
            std::vector<std::uint8_t> image;
            try {
                image = gateway::load_mz_overlay(bytes, load_segment, relocation);
            } catch (const std::out_of_range &) {
                error = 8;
            } catch (const std::runtime_error &) {
                error = 11;
            }
            if (!error && !image.empty()) {
                const auto base = std::size_t(load_segment) * 16;
                check(uc_mem_write(uc, base, image.data(), image.size()));
                check(uc_ctl_remove_cache(uc, base, base + image.size()));
            }
            set(uc, UC_X86_REG_FLAGS, (reg(uc, UC_X86_REG_FLAGS) & ~1) | bool(error));
            set(uc, UC_X86_REG_AX, error);
            if (!error)
                set(uc, UC_X86_REG_DX, 0);
        } else if (number == 0x21 && (ax >> 8) == 0x29) {
            const auto input_segment = reg(uc, UC_X86_REG_DS), offset = reg(uc, UC_X86_REG_SI);
            std::string input;
            for (unsigned i = 0; i < 260; ++i) {
                char c;
                check(uc_mem_read(uc, std::size_t(input_segment) * 16 + std::uint16_t(offset + i),
                                  &c, 1));
                input += c;
                if (!c || c == '\r')
                    break;
            }
            const auto address = std::size_t(reg(uc, UC_X86_REG_ES)) * 16 + reg(uc, UC_X86_REG_DI);
            std::array<std::uint8_t, 16> previous{};
            check(uc_mem_read(uc, address, previous.data(), previous.size()));
            const auto fcb = gateway::parse_fcb(input, ax & 255, previous);
            check(uc_mem_write(uc, address, fcb.bytes.data(), fcb.bytes.size()));
            set(uc, UC_X86_REG_SI, offset + fcb.consumed);
            set(uc, UC_X86_REG_AX, (ax & 0xff00) | fcb.status);
        } else if (number == 0x21 && (ax >> 8) == 0x1a) {
            state.dta_segment = reg(uc, UC_X86_REG_DS);
            state.dta_offset = reg(uc, UC_X86_REG_DX);
        } else if (number == 0x21 && (ax >> 8) == 0x2f) {
            set(uc, UC_X86_REG_ES, state.dta_segment);
            set(uc, UC_X86_REG_BX, state.dta_offset);
        } else if (number == 0x21 && (ax >> 8) == 0x52) {
            // DOS 5 list-of-lists subset: the first MCB segment is at ES:[BX-2].
            // Keep the information block below the process/environment allocations.
            constexpr std::uint16_t segment = 0x0800, offset = 0x0020;
            const std::uint16_t first_mcb = state.arena.blocks().front().segment - 1;
            check(uc_mem_write(uc, std::size_t(segment) * 16 + offset - 2, &first_mcb, 2));
            set(uc, UC_X86_REG_ES, segment);
            set(uc, UC_X86_REG_BX, offset);
        } else if (number == 0x21 && (ax >> 8) >= 0x48 && (ax >> 8) <= 0x4a) {
            const auto operation = ax >> 8;
            const auto result = operation == 0x48   ? state.arena.allocate(reg(uc, UC_X86_REG_BX))
                                : operation == 0x49 ? state.arena.release(reg(uc, UC_X86_REG_ES))
                                                    : state.arena.resize(reg(uc, UC_X86_REG_ES),
                                                                         reg(uc, UC_X86_REG_BX));
            if (result.error) {
                set(uc, UC_X86_REG_AX, result.error);
                if (result.error == 8)
                    set(uc, UC_X86_REG_BX, result.available);
            } else if (operation == 0x48)
                set(uc, UC_X86_REG_AX, result.segment);
            set(uc, UC_X86_REG_FLAGS, (reg(uc, UC_X86_REG_FLAGS) & ~1) | bool(result.error));
            std::vector<std::uint8_t> memory(1024 * 1024);
            state.arena.write_mcbs(memory);
            for (const auto &block : state.arena.blocks()) {
                const auto address = std::size_t(block.segment - 1) * 16;
                check(uc_mem_write(uc, address, memory.data() + address, 16));
            }
        } else if (number == 0x21 && (ax >> 8) == 0x19) {
            set(uc, UC_X86_REG_AX, (ax & 0xff00) | 2); // current drive C:
        } else if (number == 0x21 && ((ax >> 8) == 0x2b || (ax >> 8) == 0x2d)) {
            const auto cx = reg(uc, UC_X86_REG_CX), dx = reg(uc, UC_X86_REG_DX);
            const bool valid = (ax >> 8) == 0x2b
                                   ? state.clock.set_date(cx, dx >> 8, dx & 255)
                                   : state.clock.set_time(cx >> 8, cx & 255, dx >> 8, dx & 255);
            set(uc, UC_X86_REG_AX, (ax & 0xff00) | (valid ? 0 : 255));
        } else if (number == 0x21 && ((ax >> 8) == 0x2a || (ax >> 8) == 0x2c)) {
            const auto time = state.clock.local();
            if ((ax >> 8) == 0x2a) {
                set(uc, UC_X86_REG_CX, time.tm_year + 1900);
                set(uc, UC_X86_REG_DX, ((time.tm_mon + 1) << 8) | time.tm_mday);
                set(uc, UC_X86_REG_AX, (ax & 0xff00) | time.tm_wday);
            } else {
                set(uc, UC_X86_REG_CX, (time.tm_hour << 8) | time.tm_min);
                set(uc, UC_X86_REG_DX, (time.tm_sec << 8) | state.clock.hundredths());
            }
        } else if (number == 0x21 && ax == 0x4400) {
            const auto handle = reg(uc, UC_X86_REG_BX);
            const bool console = handle < state.standard_open.size() && state.standard_open[handle];
            const bool valid = console || state.files.contains(handle);
            if (valid) {
                const std::uint16_t info = !console      ? 2
                                           : handle <= 2 ? 0x80d3
                                           : handle == 3 ? 0x80c0
                                                         : 0x80a0;
                set(uc, UC_X86_REG_DX, info);
                set(uc, UC_X86_REG_AX, info);
            } else
                set(uc, UC_X86_REG_AX, 6);
            set(uc, UC_X86_REG_FLAGS, (reg(uc, UC_X86_REG_FLAGS) & ~1) | !valid);
        } else if (number == 0x21 && (ax >> 8) == 0x47) {
            const char root = 0;
            check(uc_mem_write(
                uc, std::size_t(reg(uc, UC_X86_REG_DS)) * 16 + reg(uc, UC_X86_REG_SI), &root, 1));
            set(uc, UC_X86_REG_FLAGS, reg(uc, UC_X86_REG_FLAGS) & ~1);
        } else if (number == 0x21 && (ax >> 8) == 0x43) {
            if ((ax & 255) == 1)
                throw gateway::DosIoError(5, "setting file attributes is unsupported");
            if ((ax & 255) != 0)
                throw std::runtime_error("unsupported DOS attribute operation");
            set(uc, UC_X86_REG_CX, state.drive->attributes(guest_filename(uc)));
            set(uc, UC_X86_REG_FLAGS, reg(uc, UC_X86_REG_FLAGS) & ~1);
        } else if (number == 0x21 && ((ax >> 8) == 0x3d || (ax >> 8) == 0x3c)) {
            const auto name = guest_filename(uc);
            const bool create = (ax >> 8) == 0x3c;
            std::cerr << (create ? "  create: " : "  open: ") << name << '\n';
            const auto handle = free_file_handle(state);
            state.files.emplace(handle, create ? state.drive->create(name, reg(uc, UC_X86_REG_CX))
                                               : state.drive->open(name, ax & 3));
            set(uc, UC_X86_REG_AX, handle);
            set(uc, UC_X86_REG_FLAGS, reg(uc, UC_X86_REG_FLAGS) & ~1);
        } else if (number == 0x21 && (ax >> 8) == 0x40) {
            const auto handle = reg(uc, UC_X86_REG_BX);
            auto found = state.files.find(handle);
            const bool console = (handle == 1 || handle == 2) && state.standard_open[handle];
            if (found == state.files.end() && !console)
                throw gateway::DosIoError(6, "invalid DOS write handle");
            std::vector<std::uint8_t> buffer(reg(uc, UC_X86_REG_CX));
            if (!buffer.empty())
                check(uc_mem_read(uc,
                                  std::size_t(reg(uc, UC_X86_REG_DS)) * 16 + reg(uc, UC_X86_REG_DX),
                                  buffer.data(), buffer.size()));
            std::size_t count = buffer.size();
            if (found != state.files.end())
                count = found->second.write(buffer);
            else
                (handle == 1 ? std::cout : std::cerr)
                    .write(reinterpret_cast<const char *>(buffer.data()), count);
            set(uc, UC_X86_REG_AX, count);
            set(uc, UC_X86_REG_FLAGS, reg(uc, UC_X86_REG_FLAGS) & ~1);
        } else if (number == 0x21 && (ax >> 8) == 0x3f) {
            auto found = state.files.find(reg(uc, UC_X86_REG_BX));
            if (found == state.files.end())
                throw gateway::DosIoError(6, "invalid DOS read handle");
            std::vector<std::uint8_t> buffer(reg(uc, UC_X86_REG_CX));
            const auto count = found->second.read(buffer);
            if (count)
                check(uc_mem_write(
                    uc, std::size_t(reg(uc, UC_X86_REG_DS)) * 16 + reg(uc, UC_X86_REG_DX),
                    buffer.data(), count));
            set(uc, UC_X86_REG_AX, count);
            set(uc, UC_X86_REG_FLAGS, reg(uc, UC_X86_REG_FLAGS) & ~1);
        } else if (number == 0x21 && (ax >> 8) == 0x42) {
            auto found = state.files.find(reg(uc, UC_X86_REG_BX));
            if (found == state.files.end())
                throw gateway::DosIoError(6, "invalid DOS seek handle");
            const auto offset = static_cast<std::int32_t>(
                (std::uint32_t(reg(uc, UC_X86_REG_CX)) << 16) | reg(uc, UC_X86_REG_DX));
            const auto position = found->second.seek(offset, ax & 255);
            set(uc, UC_X86_REG_AX, position & 65535);
            set(uc, UC_X86_REG_DX, position >> 16);
            set(uc, UC_X86_REG_FLAGS, reg(uc, UC_X86_REG_FLAGS) & ~1);
        } else if (number == 0x21 && (ax >> 8) == 0x3e) {
            const auto handle = reg(uc, UC_X86_REG_BX);
            bool valid = false;
            if (state.files.erase(handle))
                valid = true;
            else if (handle < state.standard_open.size()) {
                valid = state.standard_open[handle];
                state.standard_open[handle] = false;
            }
            if (!valid)
                set(uc, UC_X86_REG_AX, 6);
            set(uc, UC_X86_REG_FLAGS, (reg(uc, UC_X86_REG_FLAGS) & ~1) | !valid);
        } else if (number == 0x21 && (ax >> 8) == 0x35) {
            std::uint16_t vector[2]{};
            check(uc_mem_read(uc, (ax & 255) * 4, vector, sizeof(vector)));
            set(uc, UC_X86_REG_BX, vector[0]);
            set(uc, UC_X86_REG_ES, vector[1]);
        } else if (number == 0x21 && (ax >> 8) == 0x25) {
            const std::uint16_t vector[]{reg(uc, UC_X86_REG_DX), reg(uc, UC_X86_REG_DS)};
            check(uc_mem_write(uc, (ax & 255) * 4, vector, sizeof(vector)));
        } else if (number == 0x21 && (ax >> 8) == 0x30) {
            set(uc, UC_X86_REG_AX, 0x0005); // DOS 5.0
            set(uc, UC_X86_REG_BX, 0);
            set(uc, UC_X86_REG_CX, 0);
        } else if (number == 0x21 && (ax >> 8) == 0x09) {
            const auto base = std::size_t(reg(uc, UC_X86_REG_DS)) * 16;
            const auto offset = reg(uc, UC_X86_REG_DX);
            bool terminated = false;
            for (std::size_t i = 0; i < 65536; ++i) {
                char c;
                check(uc_mem_read(uc, base + std::uint16_t(offset + i), &c, 1));
                if (c == '$') {
                    terminated = true;
                    break;
                }
                std::cout << c;
            }
            if (!terminated)
                throw std::runtime_error("unterminated DOS output string");
        } else {
            if (number != 0x21 && dispatch(uc, number))
                return;
            state.failure = "unsupported interrupt/service (see trace)";
            check(uc_emu_stop(uc));
        }
    } catch (const gateway::DosIoError &e) {
        std::cerr << "  DOS file error " << std::dec << e.code << ": " << e.what() << std::hex
                  << '\n';
        try {
            set(uc, UC_X86_REG_AX, e.code);
            set(uc, UC_X86_REG_FLAGS, reg(uc, UC_X86_REG_FLAGS) | 1);
        } catch (const std::exception &failure) {
            hardware_failure(uc, state, failure.what());
        }
    } catch (const std::exception &e) {
        state.failure = e.what();
        uc_emu_stop(uc);
    }
}
} // namespace
int main(int argc, char **argv) {
    bool interactive = false;
#ifdef GATEWAY_SDL
    interactive = true;
#endif
    bool writable_state = false;
#ifdef GATEWAY_SDL
    writable_state = true;
#endif
    std::filesystem::path state_directory;
    unsigned milliseconds = 0;
    std::string dump_prefix;
    std::string game_args;
    std::uint16_t injected_key = 0;
    std::uint64_t breakpoint = 0;
    auto usage = [] {
        std::cout << "Usage: gateway[-probe] <DOS executable> [--headless] "
                     "[--milliseconds N] [--dump-prefix PATH] [--game-args TEXT] [--press KEY] "
                     "[--break-at HEX] [--state-dir DIR | --read-only]\n";
    };
    if (argc < 2 || std::string(argv[1]) == "--help") {
        usage();
        return argc < 2 ? 2 : 0;
    }
    for (int i = 2; i < argc; ++i) {
        const std::string option = argv[i];
        if (option == "--headless")
            interactive = false;
        else if (option == "--read-only") {
            writable_state = false;
            state_directory.clear();
        } else if (option == "--state-dir" && i + 1 < argc) {
            state_directory = argv[++i];
            if (state_directory.empty()) {
                usage();
                return 2;
            }
            writable_state = true;
        } else if (option == "--break-at" && i + 1 < argc) {
            const std::string value = argv[++i];
            const auto result =
                std::from_chars(value.data(), value.data() + value.size(), breakpoint, 16);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
                !breakpoint || breakpoint > 0xfffff) {
                usage();
                return 2;
            }
        } else if (option == "--press" && i + 1 < argc) {
            const std::string key = argv[++i];
            if (key == "escape")
                injected_key = 0x011b;
            else if (key == "space")
                injected_key = 0x3920;
            else if (key == "enter")
                injected_key = 0x1c0d;
            else {
                usage();
                return 2;
            }
        } else if (option == "--game-args" && i + 1 < argc) {
            game_args = " " + std::string(argv[++i]);
            if (game_args.size() > 126) {
                usage();
                return 2;
            }
        } else if (option == "--dump-prefix" && i + 1 < argc)
            dump_prefix = argv[++i];
        else if (option == "--milliseconds" && i + 1 < argc) {
            const std::string value = argv[++i];
            const auto result =
                std::from_chars(value.data(), value.data() + value.size(), milliseconds);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
                !milliseconds || milliseconds > 60000) {
                usage();
                return 2;
            }
        } else {
            usage();
            return 2;
        }
    }
    try {
        std::ifstream file(argv[1], std::ios::binary);
        if (!file)
            throw std::runtime_error("cannot open executable");
        std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
        auto image = gateway::load_mz(bytes);
        State state;
        if (injected_key)
            state.keys.push_back(injected_key);
        const auto tail = std::size_t(image.ds) * 16 + 0x80;
        image.memory[tail] = game_args.size();
        std::copy(game_args.begin(), game_args.end(), image.memory.begin() + tail + 1);
        image.memory[tail + 1 + game_args.size()] = 13;
        const std::uint16_t environment = 0x0fd0;
        const auto env_address = std::size_t(environment) * 16;
        image.memory[std::size_t(image.ds) * 16 + 0x2c] = environment & 255;
        image.memory[std::size_t(image.ds) * 16 + 0x2d] = environment >> 8;
        const std::string variables("PATH=C:\\\0\0", 10);
        std::copy(variables.begin(), variables.end(), image.memory.begin() + env_address);
        const auto filename = "C:\\" + std::filesystem::path(argv[1]).filename().string();
        const auto filename_address = env_address + variables.size();
        image.memory[filename_address] = 1;
        std::copy(filename.begin(), filename.end(), image.memory.begin() + filename_address + 2);
        state.arena.write_mcbs(image.memory);
        // BIOS timer trampoline: update tick count, call user INT 1Ch, then EOI/IRET.
        const std::uint8_t timer_code[]{0xcd, 0xf1, 0xcd, 0x1c, 0x50, 0xb0,
                                        0x20, 0xe6, 0x20, 0x58, 0xcf};
        std::copy(std::begin(timer_code), std::end(timer_code), image.memory.begin() + 0xf0100);
        image.memory[0xf0200] = 0xcf;
        auto vector = [&](unsigned number, std::uint16_t offset) {
            image.memory[number * 4] = offset & 255;
            image.memory[number * 4 + 1] = offset >> 8;
            image.memory[number * 4 + 2] = 0;
            image.memory[number * 4 + 3] = 0xf0;
        };
        vector(8, 0x100);
        vector(0x1c, 0x200);
        Engine engine;
        check(uc_mem_map(engine.uc, 0, image.memory.size(), UC_PROT_ALL));
        check(uc_mem_write(engine.uc, 0, image.memory.data(), image.memory.size()));
        text_mode_state(engine.uc, state, true);
        const std::uint16_t top = 0xa000;
        check(uc_mem_write(engine.uc, std::size_t(image.ds) * 16 + 2, &top, 2));
        const std::uint16_t conventional_kb = 640;
        check(uc_mem_write(engine.uc, 0x413, &conventional_kb, 2));
        set(engine.uc, UC_X86_REG_CS, image.cs);
        set(engine.uc, UC_X86_REG_IP, image.ip);
        set(engine.uc, UC_X86_REG_SS, image.ss);
        set(engine.uc, UC_X86_REG_SP, image.sp);
        set(engine.uc, UC_X86_REG_DS, image.ds);
        set(engine.uc, UC_X86_REG_ES, image.es);
        set(engine.uc, UC_X86_REG_FLAGS, 0x202);
        state.data_dir = std::filesystem::absolute(argv[1]).parent_path();
        if (writable_state && state_directory.empty()) {
            const auto xdg = std::getenv("XDG_STATE_HOME"), home = std::getenv("HOME");
            if (xdg && *xdg && std::filesystem::path(xdg).is_absolute())
                state_directory = std::filesystem::path(xdg) / "gateway-port";
            else if (home && *home)
                state_directory = std::filesystem::path(home) / ".local/state/gateway-port";
            else
                throw std::runtime_error("set --state-dir when HOME/XDG_STATE_HOME is unavailable");
        }
        state.drive = std::make_unique<gateway::DosDrive>(state.data_dir, state_directory);
        if (writable_state)
            std::cerr << "DOS state directory: " << state.drive->state_directory() << '\n';
        std::ifstream font_file(state.data_dir / "GATE_000.FNT", std::ios::binary);
        if (font_file) {
            std::vector<std::uint8_t> font{std::istreambuf_iterator<char>(font_file), {}};
            if (font.size() != 770 || font[2] != 1 || font[3] != 0x21 || font[4] != 0x7f ||
                font[6] != 8)
                throw std::runtime_error("unexpected BIOS fallback game font layout");
            for (unsigned c = 0x21; c <= 0x7f; ++c) {
                std::copy_n(font.begin() + 10 + (c - 0x21) * 8, 8, state.bios_font[c].begin());
                state.vga.text_font(c, state.bios_font[c]);
            }
            state.font_loaded = true;
        }
        uc_hook hook;
        check(uc_hook_add(engine.uc, &hook, UC_HOOK_INTR, reinterpret_cast<void *>(interrupt),
                          &state, 1, 0));
        uc_hook in_hook, out_hook;
        check(uc_hook_add(engine.uc, &in_hook, UC_HOOK_INSN, reinterpret_cast<void *>(port_in),
                          &state, 1, 0, UC_X86_INS_IN));
        check(uc_hook_add(engine.uc, &out_hook, UC_HOOK_INSN, reinterpret_cast<void *>(port_out),
                          &state, 1, 0, UC_X86_INS_OUT));
        uc_hook break_hook;
        if (breakpoint) {
            const auto callback = +[](uc_engine *uc, std::uint64_t, std::uint32_t, void *opaque) {
                static_cast<State *>(opaque)->breakpoint_hit = true;
                uc_emu_stop(uc);
            };
            check(uc_hook_add(engine.uc, &break_hook, UC_HOOK_CODE,
                              reinterpret_cast<void *>(callback), &state, breakpoint, breakpoint));
        }
        uc_hook video_hook;
        check(uc_hook_add(engine.uc, &video_hook, UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE,
                          reinterpret_cast<void *>(video_memory), &state, 0xa0000, 0xaffff));
#ifdef GATEWAY_SDL
        std::unique_ptr<gateway::SdlHost> host;
        if (interactive)
            host = std::make_unique<gateway::SdlHost>();
#endif
        bool requested_stop = false;
        uc_err status = UC_ERR_OK;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        const auto started = std::chrono::steady_clock::now();
        unsigned steps = 0;
        while (true) {
#ifdef GATEWAY_SDL
            if (host && !host->poll(state.keys)) {
                requested_stop = true;
                break;
            }
#endif
            const auto pc =
                std::size_t(reg(engine.uc, UC_X86_REG_CS)) * 16 + reg(engine.uc, UC_X86_REG_IP);
            status = uc_emu_start(engine.uc, pc, 0x100000, 100000, 5000);
            if (state.breakpoint_hit) {
                requested_stop = true;
                break;
            }
            if (status != UC_ERR_OK || state.exited || !state.failure.empty())
                break;
            state.clock.advance(1000);
            state.pit.advance(1000);
            state.opl.advance(1000);
            if (state.pit.pending() && !(state.pic_mask & 1) && !state.irq_in_service &&
                (reg(engine.uc, UC_X86_REG_FLAGS) & 0x200)) {
                state.pit.acknowledge();
                state.irq_in_service = true;
                if (!dispatch(engine.uc, 8))
                    throw std::runtime_error("missing IRQ0 vector");
            }
            ++steps;
#ifdef GATEWAY_SDL
            if (host && steps % 16 == 0) {
                text_snapshot(engine.uc, state);
                host->present(state.vga, state.opl.render(768));
                std::this_thread::sleep_until(started + std::chrono::milliseconds(steps));
            }
#endif
            if (milliseconds && steps >= milliseconds) {
                requested_stop = true;
                break;
            }
            if (!interactive && !milliseconds &&
                (steps >= 2000 || std::chrono::steady_clock::now() >= deadline))
                break;
        }
        if (!dump_prefix.empty()) {
            text_snapshot(engine.uc, state);
            std::vector<std::uint8_t> memory(1024 * 1024);
            check(uc_mem_read(engine.uc, 0, memory.data(), memory.size()));
            std::ofstream registers(dump_prefix + ".regs");
            for (const auto &[name, id] :
                 std::initializer_list<std::pair<const char *, int>>{{"AX", UC_X86_REG_AX},
                                                                     {"BX", UC_X86_REG_BX},
                                                                     {"CX", UC_X86_REG_CX},
                                                                     {"DX", UC_X86_REG_DX},
                                                                     {"CS", UC_X86_REG_CS},
                                                                     {"IP", UC_X86_REG_IP},
                                                                     {"SS", UC_X86_REG_SS},
                                                                     {"SP", UC_X86_REG_SP},
                                                                     {"DS", UC_X86_REG_DS},
                                                                     {"ES", UC_X86_REG_ES},
                                                                     {"BP", UC_X86_REG_BP},
                                                                     {"FLAGS", UC_X86_REG_FLAGS}})
                registers << name << '=' << std::hex << reg(engine.uc, id) << '\n';
            registers.close();
            std::ofstream binary(dump_prefix + ".bin", std::ios::binary);
            binary.write(reinterpret_cast<const char *>(memory.data()), memory.size());
            binary.close();
            std::ofstream picture(dump_prefix + ".ppm", std::ios::binary);
            picture << "P6\n" << state.vga.width() << ' ' << state.vga.height() << "\n255\n";
            for (auto pixel : state.vga.pixels()) {
                const char rgb[]{static_cast<char>(pixel >> 16), static_cast<char>(pixel >> 8),
                                 static_cast<char>(pixel)};
                picture.write(rgb, 3);
            }
            picture.close();
            if (!binary || !picture || !registers)
                throw std::runtime_error("cannot write diagnostic dumps");
        }
        check(status);
        if (!state.failure.empty())
            throw std::runtime_error(state.failure);
        if (requested_stop)
            return 0;
        if (!state.exited) {
            std::cerr << "Stopped at " << std::hex << reg(engine.uc, UC_X86_REG_CS) << ':'
                      << reg(engine.uc, UC_X86_REG_IP) << '\n';
            throw std::runtime_error("startup probe reached execution limit");
        }
        std::cout << "\nDOS exit code: " << state.exit_code << '\n';
        return state.exit_code ? 1 : 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
