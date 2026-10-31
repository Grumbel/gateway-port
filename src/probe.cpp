#include "dos_memory.hpp"
#include "mz.hpp"
#include "opl.hpp"
#include "vga.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
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
    gateway::DosMemory arena{0x0ff0};
    gateway::Vga vga;
    gateway::Opl opl;
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

void interrupt(uc_engine *uc, std::uint32_t number, void *opaque) noexcept {
    auto &state = *static_cast<State *>(opaque);
    try {
        auto ax = reg(uc, UC_X86_REG_AX);
        std::cerr << "INT " << std::hex << number << " AX=" << std::setw(4) << std::setfill('0')
                  << ax << " at " << reg(uc, UC_X86_REG_CS) << ':' << reg(uc, UC_X86_REG_IP)
                  << '\n';
        if (number == 0x10 && ax == 0x1a00) {
            set(uc, UC_X86_REG_AX, 0x1a);
            set(uc, UC_X86_REG_BX, 8); // VGA with an analog color display.
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
        } else if (number == 0x21 && ((ax >> 8) == 0x2a || (ax >> 8) == 0x2c)) {
            const auto now = std::chrono::system_clock::now();
            const auto seconds = std::chrono::system_clock::to_time_t(now);
            const auto *time = std::localtime(&seconds);
            if (!time)
                throw std::runtime_error("cannot read host calendar");
            if ((ax >> 8) == 0x2a) {
                set(uc, UC_X86_REG_CX, time->tm_year + 1900);
                set(uc, UC_X86_REG_DX, ((time->tm_mon + 1) << 8) | time->tm_mday);
                set(uc, UC_X86_REG_AX, (ax & 0xff00) | time->tm_wday);
            } else {
                const auto ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch())
                        .count();
                set(uc, UC_X86_REG_CX, (time->tm_hour << 8) | time->tm_min);
                set(uc, UC_X86_REG_DX, (time->tm_sec << 8) | ((ms % 1000) / 10));
            }
        } else if (number == 0x21 && ax == 0x4400) {
            const auto handle = reg(uc, UC_X86_REG_BX);
            const bool console = handle <= 2;
            const bool valid = console || state.files.contains(handle);
            if (valid) {
                const std::uint16_t info = console ? 0x80d3 : 2;
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
            if (!state.files.erase(reg(uc, UC_X86_REG_BX)))
                throw std::runtime_error("invalid close handle");
            set(uc, UC_X86_REG_FLAGS, reg(uc, UC_X86_REG_FLAGS) & ~1);
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
    const bool dump = argc == 4 && std::string(argv[2]) == "--dump-prefix";
    if ((argc != 2 && !dump) || std::string(argv[1]) == "--help") {
        std::cout << "Usage: gateway-probe <DOS executable> [--dump-prefix PATH]\n"
                     "Bounded real-mode startup trace; unsupported services stop execution.\n";
        return argc == 2 ? 0 : 2;
    }
    try {
        std::ifstream file(argv[1], std::ios::binary);
        if (!file)
            throw std::runtime_error("cannot open executable");
        std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
        auto image = gateway::load_mz(bytes);
        State state;
        state.arena.write_mcbs(image.memory);
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
        uc_hook hook;
        check(uc_hook_add(engine.uc, &hook, UC_HOOK_INTR, reinterpret_cast<void *>(interrupt),
                          &state, 1, 0));
        uc_hook in_hook, out_hook;
        check(uc_hook_add(engine.uc, &in_hook, UC_HOOK_INSN, reinterpret_cast<void *>(port_in),
                          &state, 1, 0, UC_X86_INS_IN));
        check(uc_hook_add(engine.uc, &out_hook, UC_HOOK_INSN, reinterpret_cast<void *>(port_out),
                          &state, 1, 0, UC_X86_INS_OUT));
        uc_hook video_hook;
        check(uc_hook_add(engine.uc, &video_hook, UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE,
                          reinterpret_cast<void *>(video_memory), &state, 0xa0000, 0xaffff));
        const auto status = uc_emu_start(engine.uc, std::size_t(image.cs) * 16 + image.ip, 0x100000,
                                         2000000, 10000000);
        if (dump) {
            std::vector<std::uint8_t> memory(1024 * 1024);
            check(uc_mem_read(engine.uc, 0, memory.data(), memory.size()));
            std::ofstream binary(std::string(argv[3]) + ".bin", std::ios::binary);
            binary.write(reinterpret_cast<const char *>(memory.data()), memory.size());
            binary.close();
            std::ofstream picture(std::string(argv[3]) + ".ppm", std::ios::binary);
            picture << "P6\n" << state.vga.width() << ' ' << state.vga.height() << "\n255\n";
            for (auto pixel : state.vga.pixels()) {
                const char rgb[]{static_cast<char>(pixel >> 16), static_cast<char>(pixel >> 8),
                                 static_cast<char>(pixel)};
                picture.write(rgb, 3);
            }
            picture.close();
            if (!binary || !picture)
                throw std::runtime_error("cannot write diagnostic dumps");
        }
        check(status);
        if (!state.failure.empty())
            throw std::runtime_error(state.failure);
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
