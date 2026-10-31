#include "dos_clock.hpp"
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
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
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
    std::map<unsigned, std::ifstream> files;
    unsigned next_handle = 5;
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
            const auto position = state.cursor[(bx >> 8) & 7];
            const unsigned row = position >> 8, column = position & 255;
            const auto character = ax & 255;
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
        } else if (number == 0x10 && ((ax >> 8) == 2 || (ax >> 8) == 3)) {
            const auto page = (reg(uc, UC_X86_REG_BX) >> 8) & 7;
            if ((ax >> 8) == 2)
                state.cursor[page] = reg(uc, UC_X86_REG_DX);
            else {
                set(uc, UC_X86_REG_DX, state.cursor[page]);
                set(uc, UC_X86_REG_CX, state.cursor_shape);
            }
        } else if (number == 0x10 && (ax >> 8) == 1) {
            state.cursor_shape = reg(uc, UC_X86_REG_CX);
        } else if (number == 0x10 && (ax >> 8) == 0x0f) {
            set(uc, UC_X86_REG_AX, 0x5000 | state.vga.mode());
            set(uc, UC_X86_REG_BX, reg(uc, UC_X86_REG_BX) & 255);
        } else if (number == 0x10 && (ax >> 8) == 0) {
            const auto mode = ax & 0x7f;
            if (mode != 3 && mode != 0x0d && mode != 0x0e && mode != 0x10 && mode != 0x12 &&
                mode != 0x13)
                throw std::runtime_error("unsupported VGA mode");
            state.vga.set_mode(mode, !(ax & 0x80));
            const std::uint8_t value = mode;
            check(uc_mem_write(uc, 0x449, &value, 1));
        } else if (number == 0x20 || (number == 0x21 && (ax >> 8) == 0x4c)) {
            state.exited = true;
            state.exit_code = number == 0x20 ? 0 : ax & 255;
            check(uc_emu_stop(uc));
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
        } else if (number == 0x21 && (ax >> 8) == 0x3d) {
            if ((ax & 3) != 0)
                throw std::runtime_error("probe only supports read-only opens");
            std::string name;
            bool terminated = false;
            auto address = std::size_t(reg(uc, UC_X86_REG_DS)) * 16 + reg(uc, UC_X86_REG_DX);
            for (unsigned i = 0; i < 260; ++i) {
                char c;
                check(uc_mem_read(uc, address + i, &c, 1));
                if (!c) {
                    terminated = true;
                    break;
                }
                name += c;
            }
            if (!terminated)
                throw std::runtime_error("unterminated DOS filename");
            std::cerr << "  open: " << name << '\n';
            // Map only C:\root files; never expose arbitrary host paths.
            if (name.size() >= 2 && (name[0] == 'C' || name[0] == 'c') && name[1] == ':')
                name.erase(0, 2);
            if (!name.empty() && name[0] == '\\')
                name.erase(0, 1);
            if (name.empty() || name == "." || name == ".." ||
                name.find_first_of("/\\:") != std::string::npos)
                throw std::runtime_error("probe requires a flat game-directory filename");
            auto upper = [](std::string text) {
                for (auto &c : text)
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                return text;
            };
            auto path = state.data_dir / name;
            for (const auto &entry : std::filesystem::directory_iterator(state.data_dir)) {
                if (upper(entry.path().filename().string()) == upper(name) &&
                    entry.is_regular_file() && !entry.is_symlink()) {
                    path = entry.path();
                    break;
                }
            }
            auto flags = reg(uc, UC_X86_REG_FLAGS);
            std::ifstream input;
            if (!std::filesystem::is_symlink(path))
                input.open(path, std::ios::binary);
            if (!input.is_open()) {
                set(uc, UC_X86_REG_AX, 2);
                set(uc, UC_X86_REG_FLAGS, flags | 1);
            } else {
                auto handle = state.next_handle++;
                state.files.emplace(handle, std::move(input));
                set(uc, UC_X86_REG_AX, handle);
                set(uc, UC_X86_REG_FLAGS, flags & ~1);
            }
        } else if (number == 0x21 && (ax >> 8) == 0x40) {
            const auto handle = reg(uc, UC_X86_REG_BX);
            const auto count = reg(uc, UC_X86_REG_CX);
            if (handle != 1 && handle != 2) {
                set(uc, UC_X86_REG_AX, 5);
                set(uc, UC_X86_REG_FLAGS, reg(uc, UC_X86_REG_FLAGS) | 1);
            } else {
                std::vector<char> buffer(count);
                if (count)
                    check(uc_mem_read(
                        uc, std::size_t(reg(uc, UC_X86_REG_DS)) * 16 + reg(uc, UC_X86_REG_DX),
                        buffer.data(), count));
                (handle == 1 ? std::cout : std::cerr).write(buffer.data(), count);
                set(uc, UC_X86_REG_AX, count);
                set(uc, UC_X86_REG_FLAGS, reg(uc, UC_X86_REG_FLAGS) & ~1);
            }
        } else if (number == 0x21 && (ax >> 8) == 0x3f) {
            auto found = state.files.find(reg(uc, UC_X86_REG_BX));
            if (found == state.files.end())
                throw std::runtime_error("invalid read handle");
            std::vector<char> buffer(reg(uc, UC_X86_REG_CX));
            found->second.read(buffer.data(), buffer.size());
            auto count = found->second.gcount();
            if (count)
                check(uc_mem_write(
                    uc, std::size_t(reg(uc, UC_X86_REG_DS)) * 16 + reg(uc, UC_X86_REG_DX),
                    buffer.data(), count));
            set(uc, UC_X86_REG_AX, count);
            set(uc, UC_X86_REG_FLAGS, reg(uc, UC_X86_REG_FLAGS) & ~1);
        } else if (number == 0x21 && (ax >> 8) == 0x42) {
            auto found = state.files.find(reg(uc, UC_X86_REG_BX));
            if (found == state.files.end() || (ax & 255) > 2)
                throw std::runtime_error("invalid seek handle or origin");
            const auto offset = static_cast<std::int32_t>(
                (std::uint32_t(reg(uc, UC_X86_REG_CX)) << 16) | reg(uc, UC_X86_REG_DX));
            const auto origin = (ax & 255) == 0   ? std::ios::beg
                                : (ax & 255) == 1 ? std::ios::cur
                                                  : std::ios::end;
            found->second.clear();
            found->second.seekg(offset, origin);
            if (!found->second)
                throw std::runtime_error("DOS seek failed");
            const auto position = static_cast<std::uint32_t>(found->second.tellg());
            set(uc, UC_X86_REG_AX, position & 65535);
            set(uc, UC_X86_REG_DX, position >> 16);
            set(uc, UC_X86_REG_FLAGS, reg(uc, UC_X86_REG_FLAGS) & ~1);
        } else if (number == 0x21 && (ax >> 8) == 0x3e) {
            const auto handle = reg(uc, UC_X86_REG_BX);
            bool valid = false;
            if (handle < state.standard_open.size()) {
                valid = state.standard_open[handle];
                state.standard_open[handle] = false;
            } else
                valid = state.files.erase(handle);
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
    unsigned milliseconds = 0;
    std::string dump_prefix;
    std::string game_args;
    std::uint16_t injected_key = 0;
    std::uint64_t breakpoint = 0;
    auto usage = [] {
        std::cout << "Usage: gateway[-probe] <DOS executable> [--headless] "
                     "[--milliseconds N] [--dump-prefix PATH] [--game-args TEXT] [--press KEY] [--break-at HEX]\n";
    };
    if (argc < 2 || std::string(argv[1]) == "--help") {
        usage();
        return argc < 2 ? 2 : 0;
    }
    for (int i = 2; i < argc; ++i) {
        const std::string option = argv[i];
        if (option == "--headless")
            interactive = false;
        else if (option == "--break-at" && i + 1 < argc) {
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
        std::ifstream font_file(state.data_dir / "GATE_000.FNT", std::ios::binary);
        if (font_file) {
            std::vector<std::uint8_t> font{std::istreambuf_iterator<char>(font_file), {}};
            if (font.size() != 770 || font[2] != 1 || font[3] != 0x21 || font[4] != 0x7f ||
                font[6] != 8)
                throw std::runtime_error("unexpected BIOS fallback game font layout");
            for (unsigned c = 0x21; c <= 0x7f; ++c)
                std::copy_n(font.begin() + 10 + (c - 0x21) * 8, 8, state.bios_font[c].begin());
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
