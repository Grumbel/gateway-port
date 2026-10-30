#include "mz.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
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
    bool exited = false;
    unsigned exit_code = 0;
    std::string failure;
};
std::uint32_t port_in(uc_engine *uc, std::uint32_t, int, void *opaque) noexcept {
    static_cast<State *>(opaque)->failure = "unsupported hardware port read";
    uc_emu_stop(uc);
    return 0;
}
void port_out(uc_engine *uc, std::uint32_t, int, std::uint32_t, void *opaque) noexcept {
    static_cast<State *>(opaque)->failure = "unsupported hardware port write";
    uc_emu_stop(uc);
}

void interrupt(uc_engine *uc, std::uint32_t number, void *opaque) noexcept {
    auto &state = *static_cast<State *>(opaque);
    try {
        auto ax = reg(uc, UC_X86_REG_AX);
        std::cerr << "INT " << std::hex << number << " AX=" << std::setw(4) << std::setfill('0')
                  << ax << " at " << reg(uc, UC_X86_REG_CS) << ':' << reg(uc, UC_X86_REG_IP)
                  << '\n';
        if (number == 0x20 || (number == 0x21 && (ax >> 8) == 0x4c)) {
            state.exited = true;
            state.exit_code = number == 0x20 ? 0 : ax & 255;
            check(uc_emu_stop(uc));
        } else if (number == 0x21 && (ax >> 8) == 0x19) {
            set(uc, UC_X86_REG_AX, (ax & 0xff00) | 2); // current drive C:
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
    if (argc != 2 || std::string(argv[1]) == "--help") {
        std::cout << "Usage: gateway-probe <DOS executable>\n"
                     "Bounded real-mode startup trace; unsupported services stop execution.\n";
        return argc == 2 ? 0 : 2;
    }
    try {
        std::ifstream file(argv[1], std::ios::binary);
        if (!file)
            throw std::runtime_error("cannot open executable");
        std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
        const auto image = gateway::load_mz(bytes);
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
        State state;
        state.data_dir = std::filesystem::absolute(argv[1]).parent_path();
        uc_hook hook;
        check(uc_hook_add(engine.uc, &hook, UC_HOOK_INTR, reinterpret_cast<void *>(interrupt),
                          &state, 1, 0));
        uc_hook in_hook, out_hook;
        check(uc_hook_add(engine.uc, &in_hook, UC_HOOK_INSN, reinterpret_cast<void *>(port_in),
                          &state, 1, 0, UC_X86_INS_IN));
        check(uc_hook_add(engine.uc, &out_hook, UC_HOOK_INSN, reinterpret_cast<void *>(port_out),
                          &state, 1, 0, UC_X86_INS_OUT));
        check(uc_emu_start(engine.uc, std::size_t(image.cs) * 16 + image.ip, 0x100000, 2000000,
                           10000000));
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
