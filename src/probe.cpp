#include "../third_party/font8x8/font8x8_basic.h"
#include "adlib_driver.hpp"
#include "bios_text.hpp"
#include "dos_clock.hpp"
#include "dos_fcb.hpp"
#include "dos_files.hpp"
#include "dos_memory.hpp"
#include "debug_scene.hpp"
#include "debug_state.hpp"
#include "mouse.hpp"
#include "mz.hpp"
#include "object_flags.hpp"
#include "opl.hpp"
#include "pit.hpp"
#include "rtlink.hpp"
#include "vga.hpp"
#ifdef GATEWAY_SDL
#include "sdl_host.hpp"
#endif
#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdio>
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
#include <optional>
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
    std::map<unsigned, std::string> file_names;
    std::set<unsigned> debug_pic_handles;
    std::unique_ptr<gateway::DebugSceneTracker> debug_scene;
    std::vector<std::uint8_t> debug_object_dat;
    std::optional<std::uint16_t> debug_object_ds;
    std::string debug_status;
    std::ofstream research_trace;
    unsigned elapsed_ms = 0;
    bool overlays_active = false;
    std::optional<gateway::RtlinkDirectory> overlays;
    std::vector<unsigned> overlay_loads;
    unsigned overlay_host_loads = 0;
    bool overlay_resume = false;
    std::string object_flags_mode;
    bool object_state_services = false;
    std::array<unsigned, 5> object_service_calls{};
    unsigned object_flags_calls = 0;
    bool object_flags_active = false;
    struct ObjectVerification {
        std::uint64_t exit;
        std::optional<std::uint16_t> result;
        std::optional<gateway::GuestWrite> write;
    };
    std::optional<ObjectVerification> object_verification;
    struct FarReturn {
        std::uint16_t ip, cs, sp, value;
    };
    std::optional<FarReturn> hle_return;
    struct Search {
        std::vector<gateway::DosDirectoryEntry> entries;
        std::size_t next = 0;
    };
    std::map<std::uint32_t, Search> searches;
    std::uint32_t next_search = 1;
    std::array<bool, 5> standard_open{true, true, true, true, true};
    gateway::DosMemory arena{0x0ff0, 0xa000, 0x0fd0};
    gateway::Vga vga;
    bool trace_video = false;
    bool probe_adlib = false;
    std::uint64_t adlib_calls = 0;
    gateway::Mouse mouse;
    gateway::Opl opl;
    gateway::AdlibDriver adlib{opl};
    gateway::Pit pit;
    gateway::DosClock clock;
    std::array<std::uint16_t, 8> cursor{};
    using BiosFont = std::array<std::array<std::uint8_t, 16>, 256>;
    std::map<unsigned, BiosFont> bios_fonts;
    auto font_for(unsigned height) const {
        return std::min_element(
            bios_fonts.begin(), bios_fonts.end(), [height](const auto &a, const auto &b) {
                return std::abs(int(a.first) - int(height)) < std::abs(int(b.first) - int(height));
            });
    }
    std::uint16_t cursor_shape = 0x0607;
    std::uint16_t dta_segment = 0x0ff0, dta_offset = 0x80;
    std::uint8_t pending_dos_scan = 0;
    std::deque<std::uint16_t> keys;
    std::uint8_t keyboard_data = 0;
    bool keyboard_irq_pending = false;
    std::set<unsigned> polled_services;
    std::uint8_t pic_mask = 0;
    bool irq_in_service = false;
    struct BlockTrace {
        std::uint64_t address;
        std::uint32_t size;
        std::uint16_t cs, ss, sp, bp, flags;
    };
    std::array<BlockTrace, 256> blocks{};
    std::size_t block_count = 0;
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
            if (port + i == 0x60) {
                value |= std::uint32_t(state.keyboard_data) << (8 * i);
                continue;
            }
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
            // No Sound Blaster DSP is installed. Return an empty/status-high bus
            // on its probe registers so installers can fall back to AdLib.
            if (port + i >= 0x210 && port + i <= 0x26f) {
                value |= 0xffu << (8 * i);
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
            if (port + i >= 0x210 && port + i <= 0x26f) {
                // Absent Sound Blaster/Game Blaster at scanned bases 210h
                // through 260h (each device occupies 16 ports). Old
                // detection code also writes to otherwise read-only ports;
                // an unpopulated I/O range simply ignores those writes.
                continue;
            }
            if (!state.vga.handles_port(port + i)) {
                std::ostringstream message;
                message << "unsupported hardware port write: " << std::hex << port + i;
                throw std::runtime_error(message.str());
            }
            state.vga.out(port + i, (value >> (8 * i)) & 255);
            if (state.trace_video && port + i == 0x3d5) {
                const auto index = state.vga.in(0x3d4);
                if (index == 0x07 || index == 0x09 || index == 0x0c || index == 0x0d ||
                    index == 0x13 || index == 0x17 || index == 0x18)
                    std::cerr << "VGA ms=" << std::dec << state.elapsed_ms << " CRTC["
                              << std::hex << index << "]=" << unsigned(byte) << '\n';
            }
        }
    } catch (const std::exception &e) {
        hardware_failure(uc, state, e.what());
    }
}
void video_memory(uc_engine *uc, uc_mem_type type, std::uint64_t address, int size,
                  std::int64_t value, void *opaque) noexcept {
    auto &state = *static_cast<State *>(opaque);
    if (state.vga.text_mode())
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
    const std::uint8_t mode = state.vga.mode(), page = 0, rows = 24;
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
    if (!state.vga.text_mode())
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

// Experimental section-body replacement and comparison at native load completion.
void rtlink_overlay_hook(uc_engine *uc, std::uint64_t address, std::uint32_t,
                         void *opaque) noexcept {
    auto &state = *static_cast<State *>(opaque);
    try {
        std::array<std::uint8_t, 605> code{};
        check(uc_mem_read(uc, 0x2c45c, code.data(), code.size()));
        std::uint64_t hash = 14695981039346656037ull;
        for (auto byte : code)
            hash = (hash ^ byte) * 1099511628211ull;
        if (hash != 0xce5135d0c5b49771ull || reg(uc, UC_X86_REG_CS) != 0x2c24 ||
            reg(uc, UC_X86_REG_DS) != 0x2c24) {
            if (state.overlays_active)
                throw std::runtime_error("RTLink loader fingerprint changed");
            return; // Intro code may use the same linear address before main loads.
        }
        state.overlays_active = true;
        if (!state.overlays) {
            state.overlays = gateway::parse_rtlink(state.drive->read_all("GATEMAIN.EXE"),
                                                   state.drive->read_all("GATEMAIN.OVL"));
            state.overlay_loads.resize(state.overlays->sections.size());
        }
        std::uint16_t record;
        check(uc_mem_read(uc, 0x2c240 + 0xa8a, &record, 2));
        if (record < 0xc1f || (record - 0xc1f) % 18)
            throw std::runtime_error("invalid live RTLink section record");
        const auto id = unsigned(record - 0xc1f) / 18;
        if (id >= state.overlays->sections.size())
            throw std::runtime_error("live RTLink section index outside directory");
        const auto &section = state.overlays->sections[id];
        const auto base = std::size_t(section.linked_segment + 0x1000) * 16;
        if (base + section.memory_size() > 0xa0000)
            throw std::runtime_error("live RTLink section outside conventional memory");
        if (address == 0x2c516) {
            // The native file helper has run. Keep all surrounding residency/return machinery.
            std::uint16_t destination;
            check(uc_mem_read(uc, 0x2c240 + record, &destination, 2));
            if (std::size_t(destination) * 16 != base)
                throw std::runtime_error("RTLink host load cannot use a remapped destination");
            std::vector<std::uint8_t> memory(0xa0000);
            check(uc_mem_read(uc, base, memory.data() + base, section.memory_size()));
            gateway::apply_rtlink(memory, section, 0x1000);
            check(uc_mem_write(uc, base, memory.data() + base, section.memory_size()));
            check(uc_ctl_remove_cache(uc, base, base + section.memory_size()));
            ++state.overlay_host_loads;
            if (state.research_trace.is_open())
                state.research_trace << "overlay-host ms=" << std::dec << state.elapsed_ms
                                     << std::hex << " section=" << id << " address=" << base
                                     << '\n';
            state.overlay_resume = true;
            check(uc_emu_stop(uc));
            return;
        }
        const auto expected = gateway::relocate_rtlink(section, 0x1000);
        std::vector<std::uint8_t> actual(section.memory_size());
        check(uc_mem_read(uc, base, actual.data(), actual.size()));
        for (std::size_t i = 0; i < expected.size(); ++i)
            if (expected[i] != actual[i])
                throw std::runtime_error("RTLink relocated byte mismatch in section " +
                                         std::to_string(id) + " at offset " + std::to_string(i));
        for (auto i = section.initialized_size(); i < section.memory_size(); ++i)
            if (actual[i] != 0)
                throw std::runtime_error("RTLink BSS mismatch in section " + std::to_string(id));
        ++state.overlay_loads[id];
        if (state.research_trace.is_open())
            state.research_trace << "overlay-load ms=" << std::dec << state.elapsed_ms << std::hex
                                 << " section=" << id << " address=" << base
                                 << " file_offset=" << section.image_file_offset
                                 << " size=" << expected.size()
                                 << " relocations=" << section.fixups.size() << '\n';
    } catch (const std::exception &e) {
        hardware_failure(uc, state, e.what());
    }
}

enum class ObjectService { Flag, SetFlag, ClearFlag, Location, SetLocation };
struct ObjectServiceSpec {
    std::uint64_t entry, exit;
    const char *name;
};
constexpr std::array<ObjectServiceSpec, 5> object_services{
    {{0x11916, 0x1194c, "object-flag"},
     {0x11950, 0x1197f, "object-flag-set"},
     {0x11983, 0x119b4, "object-flag-clear"},
     {0x11ab6, 0x11b2c, "object-location"},
     {0x11bbb, 0x11c36, "object-location-set"}}};

// Fixed addresses are opt-in and guarded by fingerprints of the complete routines.
void object_flags_hook(uc_engine *uc, std::uint64_t address, std::uint32_t, void *opaque) noexcept {
    auto &state = *static_cast<State *>(opaque);
    try {
        const gateway::GuestByteReader read = [uc](std::uint16_t segment, std::uint32_t offset) {
            const auto address = std::size_t(segment) * 16 + offset;
            if (address >= 0xa0000)
                throw std::runtime_error("object state read outside conventional memory");
            std::uint8_t byte;
            check(uc_mem_read(uc, address, &byte, 1));
            return byte;
        };
        const auto spec = std::find_if(
            object_services.begin(), object_services.end(),
            [address](const auto &spec) { return spec.entry == address || spec.exit == address; });
        if (spec == object_services.end())
            throw std::runtime_error("unknown object service hook");
        if (address == spec->exit) {
            if (state.object_verification) {
                const auto expected = *state.object_verification;
                state.object_verification.reset();
                if (expected.exit != address ||
                    (expected.result && reg(uc, UC_X86_REG_AX) != *expected.result))
                    throw std::runtime_error("object service HLE disagrees with native result");
                if (expected.write)
                    for (unsigned i = 0; i < expected.write->size; ++i)
                        if (read(expected.write->segment, std::uint32_t(expected.write->offset) +
                                                              i) != expected.write->bytes[i])
                            throw std::runtime_error(
                                "object service HLE disagrees with native write");
            }
            return;
        }
        std::array<std::uint8_t, 1070> code{};
        const auto size = state.object_state_services ? code.size() : 323;
        check(uc_mem_read(uc, 0x1180d, code.data(), size));
        std::uint64_t hash = 14695981039346656037ull;
        for (std::size_t i = 0; i < size; ++i)
            hash = (hash ^ code[i]) * 1099511628211ull;
        const auto expected_hash =
            state.object_state_services ? 0xd7176aec031d44a3ull : 0x2865a272e07a21e5ull;
        if (hash != expected_hash || reg(uc, UC_X86_REG_CS) != 0x1157) {
            if (state.object_flags_active)
                throw std::runtime_error("object service routine fingerprint changed");
            return; // Intro code can occupy this address before GATEMAIN is loaded.
        }
        state.object_flags_active = true;
        const auto ss = reg(uc, UC_X86_REG_SS), sp = reg(uc, UC_X86_REG_SP);
        const auto word = [&](unsigned offset) {
            return std::uint16_t(read(ss, std::uint16_t(sp + offset)) |
                                 (std::uint16_t(read(ss, std::uint16_t(sp + offset + 1))) << 8));
        };
        const auto object = word(4);
        const auto ds = reg(uc, UC_X86_REG_DS);
        const auto index = std::size_t(spec - object_services.begin());
        const auto service = static_cast<ObjectService>(index);
        // Location has one argument; do not interpret the next caller stack word as an argument.
        const auto argument = service == ObjectService::Location ? 0 : word(6);
        std::optional<std::uint16_t> result;
        std::optional<gateway::GuestWrite> write;
        switch (service) {
        case ObjectService::Flag:
            result = gateway::object_flag(read, ds, object, argument);
            break;
        case ObjectService::SetFlag:
        case ObjectService::ClearFlag:
            write = gateway::object_flag_write(read, ds, object, argument,
                                               service == ObjectService::SetFlag);
            break;
        case ObjectService::Location:
            result = gateway::object_location(read, ds, object);
            break;
        case ObjectService::SetLocation:
            write = gateway::object_location_write(read, ds, object, argument);
            break;
        }
        ++state.object_service_calls[index];
        if (service == ObjectService::Flag)
            ++state.object_flags_calls;
        if (state.research_trace.is_open()) {
            auto &out = state.research_trace;
            out << spec->name << " ms=" << std::dec << state.elapsed_ms << std::hex
                << " object=" << object;
            if (service != ObjectService::Location)
                out << (service == ObjectService::SetLocation ? " location=" : " property=")
                    << argument;
            if (result)
                out << " result=" << *result;
            if (write) {
                out << " write=" << write->segment << ':' << write->offset << ':';
                for (unsigned i = 0; i < write->size; ++i)
                    out << unsigned(write->bytes[i]) << ',';
            }
            out << " return=" << word(2) << ':' << word(0) << '\n';
        }
        if (state.object_flags_mode == "verify") {
            if (state.object_verification)
                throw std::runtime_error("nested object service verification is unsupported");
            state.object_verification = State::ObjectVerification{spec->exit, result, write};
        } else {
            if (write) {
                const auto address = std::size_t(write->segment) * 16 + write->offset;
                if (address + write->size > 0xa0000)
                    throw std::runtime_error("object state write outside conventional memory");
                check(uc_mem_write(uc, address, write->bytes.data(), write->size));
            }
            state.hle_return = State::FarReturn{word(0), word(2), std::uint16_t(sp + 4),
                                                result.value_or(reg(uc, UC_X86_REG_AX))};
            check(uc_emu_stop(uc));
        }
    } catch (const std::exception &e) {
        hardware_failure(uc, state, e.what());
    }
}

// Opt-in entry snapshots: stack words retain far return addresses and arguments.
void trace_entry(uc_engine *uc, std::uint64_t address, std::uint32_t, void *opaque) noexcept {
    auto &state = *static_cast<State *>(opaque);
    try {
        auto &out = state.research_trace;
        out << "entry ms=" << std::dec << state.elapsed_ms << std::hex << " pc=" << address
            << " cs=" << reg(uc, UC_X86_REG_CS) << " ds=" << reg(uc, UC_X86_REG_DS)
            << " es=" << reg(uc, UC_X86_REG_ES) << " ax=" << reg(uc, UC_X86_REG_AX)
            << " dx=" << reg(uc, UC_X86_REG_DX) << " ss=" << reg(uc, UC_X86_REG_SS)
            << " sp=" << reg(uc, UC_X86_REG_SP) << " bp=" << reg(uc, UC_X86_REG_BP) << " stack=";
        const auto base = std::size_t(reg(uc, UC_X86_REG_SS)) * 16;
        for (unsigned i = 0; i < 32; ++i) {
            std::uint16_t word;
            check(uc_mem_read(uc, base + std::uint16_t(reg(uc, UC_X86_REG_SP) + i * 2), &word, 2));
            out << word << ',';
        }
        out << " frames=";
        auto bp = reg(uc, UC_X86_REG_BP);
        for (unsigned i = 0; i < 8 && bp <= 0xfff9; ++i) {
            std::array<std::uint16_t, 3> words{};
            check(uc_mem_read(uc, base + bp, words.data(), 6));
            out << bp << ':' << words[0] << ':' << words[1] << ':' << words[2] << ',';
            if (words[0] <= bp)
                break;
            bp = words[0];
        }
        out << '\n';
        if (!out)
            throw std::runtime_error("cannot write research trace");
    } catch (const std::exception &e) {
        hardware_failure(uc, state, e.what());
    }
}

void trace_block(uc_engine *uc, std::uint64_t address, std::uint32_t size, void *opaque) noexcept {
    auto &state = *static_cast<State *>(opaque);
    try {
        state.blocks[state.block_count++ % state.blocks.size()] = {address,
                                                                   size,
                                                                   reg(uc, UC_X86_REG_CS),
                                                                   reg(uc, UC_X86_REG_SS),
                                                                   reg(uc, UC_X86_REG_SP),
                                                                   reg(uc, UC_X86_REG_BP),
                                                                   reg(uc, UC_X86_REG_FLAGS)};
    } catch (const std::exception &e) {
        hardware_failure(uc, state, e.what());
    }
}

void interrupt(uc_engine *uc, std::uint32_t number, void *opaque) noexcept {
    auto &state = *static_cast<State *>(opaque);
    try {
        auto ax = reg(uc, UC_X86_REG_AX);
        const bool frequent = number == 0x33 || number == 0x16 || number == 0x65 ||
                              number == 0xf1 ||
                              number == 0x1c ||
                              (number == 0x21 && ((ax >> 8) == 0x2a || (ax >> 8) == 0x2c));
        const auto service = number == 0x33 ? ax : number == 0x65 ? reg(uc, UC_X86_REG_SI)
                                                               : ax >> 8;
        const bool first =
            !frequent || state.polled_services.insert((number << 16) | service).second;
        if (first)
            std::cerr << "INT " << std::hex << number << " AX=" << std::setw(4) << std::setfill('0')
                      << ax << " BX=" << reg(uc, UC_X86_REG_BX) << " CX=" << reg(uc, UC_X86_REG_CX)
                      << " DX=" << reg(uc, UC_X86_REG_DX) << " at " << reg(uc, UC_X86_REG_CS) << ':'
                      << reg(uc, UC_X86_REG_IP) << '\n';
        if (number == 0x33) {
            auto &mouse = state.mouse;
            if (ax == 0 || ax == 0x21) {
                mouse = gateway::Mouse{};
                mouse.max_y = state.vga.text_mode() ? 199 : state.vga.height() - 1;
                mouse.y = (mouse.max_y + 1) / 2;
                set(uc, UC_X86_REG_AX, 0xffff);
                set(uc, UC_X86_REG_BX, 3);
            } else if (ax == 0x20) {
                // Enable driver after a previous disable. Input is serviced by SDL.
            } else if (ax == 1) {
                mouse.visibility = std::min(0, mouse.visibility + 1);
            } else if (ax == 2) {
                mouse.visibility = std::max(-32768, mouse.visibility - 1);
            } else if (ax == 3) {
                set(uc, UC_X86_REG_BX, mouse.buttons);
                set(uc, UC_X86_REG_CX, mouse.x);
                set(uc, UC_X86_REG_DX, mouse.y);
            } else if (ax == 4) {
                mouse.position(reg(uc, UC_X86_REG_CX), reg(uc, UC_X86_REG_DX));
                mouse.warp_pending = true;
            } else if (ax == 5 || ax == 6) {
                const auto button = reg(uc, UC_X86_REG_BX);
                if (button >= mouse.history.size())
                    throw std::runtime_error("unsupported mouse button");
                auto &entry = mouse.history[button];
                set(uc, UC_X86_REG_AX, mouse.buttons);
                set(uc, UC_X86_REG_BX, ax == 5 ? entry.presses : entry.releases);
                set(uc, UC_X86_REG_CX, ax == 5 ? entry.press_x : entry.release_x);
                set(uc, UC_X86_REG_DX, ax == 5 ? entry.press_y : entry.release_y);
                (ax == 5 ? entry.presses : entry.releases) = 0;
            } else if (ax == 7 || ax == 8) {
                int low = reg(uc, UC_X86_REG_CX), high = reg(uc, UC_X86_REG_DX);
                if (low > high)
                    std::swap(low, high);
                (ax == 7 ? mouse.min_x : mouse.min_y) = low;
                (ax == 7 ? mouse.max_x : mouse.max_y) = high;
                mouse.position(mouse.x, mouse.y);
            } else if (ax == 0xb) {
                set(uc, UC_X86_REG_CX, mouse.motion_x);
                set(uc, UC_X86_REG_DX, mouse.motion_y);
                mouse.motion_x = mouse.motion_y = 0;
            } else {
                throw std::runtime_error("unsupported mouse service (see trace)");
            }
        } else if (number == 0x21 && ((ax >> 8) == 7 || (ax >> 8) == 8 || (ax >> 8) == 1)) {
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
        } else if (number == 0x1a && (ax >> 8) == 0) {
            std::uint32_t ticks;
            std::uint8_t midnight;
            check(uc_mem_read(uc, 0x46c, &ticks, sizeof(ticks)));
            check(uc_mem_read(uc, 0x470, &midnight, sizeof(midnight)));
            set(uc, UC_X86_REG_CX, ticks >> 16);
            set(uc, UC_X86_REG_DX, ticks & 0xffff);
            set(uc, UC_X86_REG_AX, midnight);
            midnight = 0;
            check(uc_mem_write(uc, 0x470, &midnight, sizeof(midnight)));
        } else if (number == 0xf1 && reg(uc, UC_X86_REG_CS) == 0xf000) {
            std::uint32_t ticks;
            check(uc_mem_read(uc, 0x46c, &ticks, 4));
            ticks = (ticks + 1) % 0x1800b0;
            check(uc_mem_write(uc, 0x46c, &ticks, 4));
            if (ticks == 0) {
                const std::uint8_t midnight = 1;
                check(uc_mem_write(uc, 0x470, &midnight, sizeof(midnight)));
            }
        } else if (number == 0x10 && ax == 0x1a00) {
            set(uc, UC_X86_REG_AX, 0x1a);
            set(uc, UC_X86_REG_BX, 8); // VGA with an analog color display.
        } else if (number == 0x10 && ax == 0x1b02) {
            // VGA functionality/state information only defines AL=00. A
            // nonzero AL is not a supported subfunction; report that through
            // the BIOS signature instead of terminating the guest.
            set(uc, UC_X86_REG_AX, ax & 0xff00);
        } else if (number == 0x10 && (ax >> 8) == 0x12 &&
                   (reg(uc, UC_X86_REG_BX) & 255) == 0x10) {
            // Return EGA/VGA configuration: color adapter with 256 KiB VRAM.
            // Keep CX nonzero so software detects the adapter as installed.
            set(uc, UC_X86_REG_BX, 0x0003);
            set(uc, UC_X86_REG_CX, 0x0009);
        } else if (number == 0x10 && ax == 0xef00) {
            // Nonstandard video BIOS extension queried by Spellcasting 101.
            // On an ordinary VGA BIOS, unknown functions return unchanged.
        } else if (number == 0x10 && ax == 0x1130) {
            // Get BIOS font information. Fixed ROM-like font tables are
            // installed in guest memory during initialization below.
            const auto selector = reg(uc, UC_X86_REG_BX) >> 8;
            const std::uint16_t font_segment =
                selector == 2 || selector == 5 ? 0xf500 : selector == 1 || selector == 6
                                                              ? 0xf600
                                                              : 0xf400;
            if (selector > 6)
                throw std::runtime_error("unsupported BIOS font selector");
            set(uc, UC_X86_REG_ES, font_segment);
            set(uc, UC_X86_REG_BP, selector == 4 ? 128 * 8 : 0);
            set(uc, UC_X86_REG_CX, state.vga.text_mode() ? 16 :
                                    state.vga.mode() == 0x12 ? 16 :
                                    state.vga.mode() == 0x10 ? 14 : 8);
            set(uc, UC_X86_REG_DX, (reg(uc, UC_X86_REG_DX) & 0xff00) | 24);
        } else if (number == 0x10 && ax == 0x1002) {
            // EGA/VGA: ES:DX addresses 16 attribute palette entries followed
            // by the overscan color register.
            std::array<std::uint8_t, 17> values{};
            check(uc_mem_read(uc, std::size_t(reg(uc, UC_X86_REG_ES)) * 16 +
                                      reg(uc, UC_X86_REG_DX), values.data(), values.size()));
            state.vga.attribute_palette(values);
        } else if (number == 0x10 && (ax >> 8) == 9) {
            const auto bx = reg(uc, UC_X86_REG_BX);
            const auto page = (bx >> 8) & 7;
            const auto position = state.cursor[page];
            const unsigned row = position >> 8, column = position & 255;
            const auto character = ax & 255;
            if (state.vga.text_mode()) {
                const std::uint8_t cell[]{static_cast<std::uint8_t>(character),
                                          static_cast<std::uint8_t>(bx)};
                const auto first = row * 80 + column;
                for (unsigned i = 0; i < reg(uc, UC_X86_REG_CX) && first + i < 2000; ++i)
                    check(uc_mem_write(uc, 0xb8000 + page * 4096 + (first + i) * 2, cell, 2));
            } else {
                const unsigned height = state.vga.mode() == 0x12   ? 16
                                        : state.vga.mode() == 0x10 ? 14
                                                                   : 8;
                const auto font = state.font_for(height);
                if (font == state.bios_fonts.end() && character != ' ')
                    throw std::runtime_error(
                        "BIOS glyph needs a fixed 8-pixel game font (*_000/100/200.FNT)");
                std::array<std::uint8_t, 16> glyph{};
                if (font != state.bios_fonts.end())
                    for (unsigned i = 0; i < height; ++i)
                        glyph[i] = font->second[character][i * font->first / height];
                for (unsigned i = 0; i < reg(uc, UC_X86_REG_CX); ++i)
                    state.vga.draw_glyph(column + i, row, std::span(glyph).first(height),
                                         bx & 255, page);
            }
        } else if (number == 0x10 && (ax >> 8) == 8 && state.vga.text_mode()) {
            const auto page = (reg(uc, UC_X86_REG_BX) >> 8) & 7;
            const auto position = state.cursor[page];
            const auto cell = (position >> 8) * 80 + (position & 255);
            std::uint16_t value = 0;
            if (cell < 2000)
                check(uc_mem_read(uc, 0xb8000 + page * 4096 + cell * 2, &value, 2));
            set(uc, UC_X86_REG_AX, value);
        } else if (number == 0x10 && ((ax >> 8) == 6 || (ax >> 8) == 7)) {
            const auto bx = reg(uc, UC_X86_REG_BX);
            const auto cx = reg(uc, UC_X86_REG_CX);
            const auto dx = reg(uc, UC_X86_REG_DX);
            if (state.vga.text_mode()) {
                std::array<std::uint8_t, 4096> page{};
                const auto address = 0xb8000 + state.vga.text_page() * page.size();
                check(uc_mem_read(uc, address, page.data(), page.size()));
                gateway::scroll_text_window(page, (ax >> 8) == 7, ax & 255, bx >> 8,
                                            cx >> 8, cx & 255, dx >> 8, dx & 255);
                check(uc_mem_write(uc, address, page.data(), page.size()));
            } else {
                state.vga.scroll_graphics_window((ax >> 8) == 7, ax & 255, bx >> 8,
                                                 cx >> 8, cx & 255, dx >> 8, dx & 255);
            }
        } else if (number == 0x10 && (ax >> 8) == 5) {
            const std::uint8_t page = ax & 7;
            if (state.vga.text_mode())
                state.vga.text_page(page);
            else
                state.vga.graphics_page(page);
            if (state.trace_video)
                std::cerr << "VGA ms=" << std::dec << state.elapsed_ms << " BIOS page="
                          << unsigned(page) << " mode=" << std::hex
                          << unsigned(state.vga.mode()) << std::dec
                          << " page_bytes=" << state.vga.page_size()
                          << " start_bytes=" << page * state.vga.page_size() << '\n';
            const std::uint16_t start = state.vga.text_mode()
                                            ? page * 4096
                                            : page * state.vga.page_size();
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
                    ((state.vga.text_mode() ? state.vga.text_page()
                                            : state.vga.graphics_page()) << 8));
        } else if (number == 0x10 && (ax >> 8) == 0) {
            const auto mode = ax & 0x7f;
            if (mode != 2 && mode != 3 && mode != 0x0d && mode != 0x0e && mode != 0x10 &&
                mode != 0x12 && mode != 0x13)
                throw std::runtime_error("unsupported VGA mode");
            state.vga.set_mode(mode, !(ax & 0x80));
            if (state.trace_video)
                std::cerr << "VGA ms=" << std::dec << state.elapsed_ms << " BIOS mode="
                          << std::hex << unsigned(mode) << '\n';
            if (state.vga.text_mode())
                text_mode_state(uc, state, !(ax & 0x80));
            const std::uint8_t value = mode;
            check(uc_mem_write(uc, 0x449, &value, 1));
            const std::uint16_t page_size = state.vga.page_size();
            check(uc_mem_write(uc, 0x44c, &page_size, 2));
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
            state.file_names[handle] = name;
            if (state.debug_scene && !create) {
                const auto region = gateway::DebugSceneTracker::matching_rgn(name);
                if (!region.empty()) {
                    try {
                        if (state.debug_scene->add_picture(name, state.drive->read_all(name),
                                                           state.drive->read_all(region)))
                            state.debug_pic_handles.insert(handle);
                    } catch (const std::exception &) {
                        // Not every PIC series has an RGN. Debug discovery must
                        // never affect the game's DOS file-open result.
                    }
                }
            }
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
            const auto source = (state.research_trace.is_open() ||
                                 state.debug_pic_handles.contains(reg(uc, UC_X86_REG_BX)))
                                    ? found->second.seek(0, 1)
                                    : 0;
            const auto count = found->second.read(buffer);
            if (state.debug_scene &&
                state.debug_pic_handles.contains(reg(uc, UC_X86_REG_BX)))
                state.debug_scene->note_read(state.file_names[reg(uc, UC_X86_REG_BX)], source,
                                             count);
            if (state.research_trace.is_open())
                state.research_trace
                    << "read ms=" << std::dec << state.elapsed_ms << std::hex
                    << " file=" << state.file_names[reg(uc, UC_X86_REG_BX)] << " offset=" << source
                    << " size=" << count << " address="
                    << std::size_t(reg(uc, UC_X86_REG_DS)) * 16 + reg(uc, UC_X86_REG_DX) << '\n';
            if (count) {
                const auto address =
                    std::size_t(reg(uc, UC_X86_REG_DS)) * 16 + reg(uc, UC_X86_REG_DX);
                check(uc_mem_write(uc, address, buffer.data(), count));
                // RTLink also loads executable overlays through ordinary DOS reads.
                check(uc_ctl_remove_cache(uc, address, address + count));
            }
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
            state.file_names.erase(handle);
            state.debug_pic_handles.erase(handle);
            if (state.files.erase(handle))
                valid = true;
            else if (handle < state.standard_open.size()) {
                valid = state.standard_open[handle];
                state.standard_open[handle] = false;
            }
            if (!valid)
                set(uc, UC_X86_REG_AX, 6);
            set(uc, UC_X86_REG_FLAGS, (reg(uc, UC_X86_REG_FLAGS) & ~1) | !valid);
        } else if (number == 0x65 && state.probe_adlib) {
            const auto function = reg(uc, UC_X86_REG_SI);
            const auto args = std::size_t(reg(uc, UC_X86_REG_ES)) * 16 +
                              reg(uc, UC_X86_REG_BX);
            ++state.adlib_calls;
            std::array<std::uint8_t, 16> data{};
            check(uc_mem_read(uc, args, data.data(), data.size()));
            if (state.research_trace.is_open()) {
                state.research_trace << "adlib ms=" << std::dec << state.elapsed_ms
                                     << " si=" << std::hex << function << " es:bx="
                                     << reg(uc, UC_X86_REG_ES) << ':' << reg(uc, UC_X86_REG_BX)
                                     << " args=";
                for (auto byte : data)
                    state.research_trace << std::setw(2) << std::setfill('0')
                                         << unsigned(byte);
                state.research_trace << '\n';
            }
            std::array<std::uint8_t, 56> instrument{};
            if ((function & 255) == 0x10) {
                const std::size_t address =
                    (std::size_t(data[2] | (unsigned(data[3]) << 8)) << 4) +
                    (data[0] | (unsigned(data[1]) << 8));
                // A guest may pass an invalid instrument during interface
                // detection; keep the event contract without crashing.
                uc_mem_read(uc, address, instrument.data(), instrument.size());
            }
            set(uc, UC_X86_REG_AX, state.adlib.call(function, data, ax, instrument));
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

// Used both by --dump-prefix and the F10 menu. The menu's extended files are
// observations of guest-owned state, not a standalone/recovered story model.
void dump_guest(uc_engine *uc, State &state, const std::string &prefix) {
    text_snapshot(uc, state);
    std::vector<std::uint8_t> memory(1024 * 1024);
    check(uc_mem_read(uc, 0, memory.data(), memory.size()));
    std::ofstream registers(prefix + ".regs");
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
        registers << name << '=' << std::hex << reg(uc, id) << '\n';
    registers.close();
    std::ofstream binary(prefix + ".bin", std::ios::binary);
    binary.write(reinterpret_cast<const char *>(memory.data()), memory.size());
    binary.close();
    std::ofstream picture(prefix + ".ppm", std::ios::binary);
    picture << "P6\n" << state.vga.width() << ' ' << state.vga.height() << "\n255\n";
    for (auto pixel : state.vga.pixels()) {
        const char rgb[]{static_cast<char>(pixel >> 16), static_cast<char>(pixel >> 8),
                         static_cast<char>(pixel)};
        picture.write(rgb, 3);
    }
    picture.close();
    if (!binary || !picture || !registers)
        throw std::runtime_error("cannot write diagnostic dumps");

    if (!state.debug_scene)
        return;
    const auto scene = state.debug_scene->view();
    std::ofstream regions(prefix + ".regions.tsv");
    regions << "picture\tregion_file\tentry\tengine_object_id\tname\tx0\ty0\tx1\ty1\tvertices\n";
    for (const auto &box : scene.boxes) {
        regions << scene.picture << '\t' << scene.region_file << '\t' << scene.entry << '\t'
                << box.object_id << '\t' << box.name << '\t' << box.x0 << '\t' << box.y0
                << '\t' << box.x1 << '\t' << box.y1 << '\t';
        for (std::size_t i = 0; i < box.polygon.size(); ++i) {
            if (i)
                regions << ';';
            regions << box.polygon[i].x << ',' << box.polygon[i].y;
        }
        regions << '\n';
    }
    regions.close();
    std::ofstream metadata(prefix + ".debug.txt");
    metadata << "guest_ms=" << std::dec << state.elapsed_ms << '\n'
             << "picture=" << scene.picture << '\n'
             << "rgn=" << scene.region_file << '\n'
             << "rgn_entry=" << scene.entry << '\n'
             << "rgn_entry_count=" << scene.entry_count << '\n'
             << "scene_selection=" << (scene.auto_follow ? "DOS PIC read estimate" : "manual")
             << '\n' << "region_x=scene-local pixels (packed coordinates already doubled)\n"
             << "region_geometry=rectangles or ordered Gateway II polygon vertices\n"
             << "region_object_id=native engine object ID, NOT OBJECT.DAT list index\n";
    auto objects = gateway::inspect_gateway_state(memory, reg(uc, UC_X86_REG_DS));
    if ((!objects || objects->objects.size() != 734) && state.debug_object_ds)
        objects = gateway::inspect_gateway_state(memory, *state.debug_object_ds);
    if (objects && objects->objects.size() == 734) {
        std::ofstream records(prefix + ".objects.tsv");
        records << gateway::debug_objects_tsv(*objects, state.debug_object_dat);
        records.close();
        if (!records)
            throw std::runtime_error("cannot write object state dump");
        metadata << "object_count=" << objects->objects.size() << '\n';
        metadata << "object_ds=" << std::hex << objects->ds << '\n';
        if (objects->book_location)
            metadata << "book_location=" << std::dec << *objects->book_location << '\n';
        if (objects->drawer_open)
            metadata << "drawer_open=" << *objects->drawer_open << '\n';
    } else {
        metadata << "object_count=unknown (Gateway object table not detected at DS)\n";
    }
    metadata.close();
    if (!regions || !metadata)
        throw std::runtime_error("cannot write debug scene metadata");
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
    std::filesystem::path debug_directory;
    unsigned milliseconds = 0;
    std::string dump_prefix, block_trace_path;
    std::string game_args, research_path, object_flags_mode;
    bool object_state_services = false;
    bool verify_overlays = false, hle_overlays = false, trace_video = false;
    bool trace_timing = false;
    bool probe_adlib = false;
    unsigned aspect_width = 4, aspect_height = 3;
    std::vector<std::uint64_t> entry_addresses;
    std::multimap<unsigned, std::uint16_t> scheduled_keys;
    std::uint16_t injected_key = 0;
    std::uint64_t breakpoint = 0;
    auto usage = [] {
        std::cout << "Usage: gateway[-probe] <DOS executable> [--headless] "
                     "[--milliseconds N] [--dump-prefix PATH] [--game-args TEXT] [--press KEY] "
                     "[--break-at HEX] [--state-dir DIR | --read-only] [--trace-blocks FILE] "
                     "[--trace-research FILE --trace-entry HEX ...] [--key-at MS:BIOSHEX ...] "
                     "[--object-flags verify|hle | --object-state verify|hle] [--verify-overlays] "
                     "[--hle-overlays] [--trace-video] [--trace-timing] [--adlib-driver] "
                     "[--aspect-ratio native|W:H] [--debug-dir DIR]\n";
    };
    if (argc < 2 || std::string(argv[1]) == "--help") {
        usage();
        return argc < 2 ? 2 : 0;
    }
    for (int i = 2; i < argc; ++i) {
        const std::string option = argv[i];
        if (option == "--hle-overlays") {
            hle_overlays = true;
            verify_overlays = true;
        } else if (option == "--trace-video")
            trace_video = true;
        else if (option == "--trace-timing")
            trace_timing = true;
        else if (option == "--probe-adlib" || option == "--adlib-driver")
            probe_adlib = true;
        else if (option == "--aspect-ratio" && i + 1 < argc) {
            const std::string value = argv[++i];
            if (value == "native") {
                aspect_width = aspect_height = 0;
            } else {
                const auto separator = value.find(':');
                if (separator == std::string::npos) {
                    usage();
                    return 2;
                }
                const auto first = std::from_chars(value.data(), value.data() + separator,
                                                   aspect_width);
                const auto second = std::from_chars(value.data() + separator + 1,
                                                    value.data() + value.size(), aspect_height);
                if (first.ec != std::errc{} || first.ptr != value.data() + separator ||
                    second.ec != std::errc{} || second.ptr != value.data() + value.size() ||
                    !aspect_width || !aspect_height || aspect_width > 10000 ||
                    aspect_height > 10000) {
                    usage();
                    return 2;
                }
            }
        } else if (option == "--verify-overlays")
            verify_overlays = true;
        else if ((option == "--object-flags" || option == "--object-state") && i + 1 < argc) {
            if (!object_flags_mode.empty()) {
                usage();
                return 2;
            }
            object_state_services = option == "--object-state";
            object_flags_mode = argv[++i];
            if (object_flags_mode != "verify" && object_flags_mode != "hle") {
                usage();
                return 2;
            }
        } else if (option == "--trace-research" && i + 1 < argc)
            research_path = argv[++i];
        else if (option == "--trace-entry" && i + 1 < argc) {
            const std::string value = argv[++i];
            std::uint64_t address = 0;
            const auto result =
                std::from_chars(value.data(), value.data() + value.size(), address, 16);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
                address > 0xfffff) {
                usage();
                return 2;
            }
            entry_addresses.push_back(address);
        } else if (option == "--key-at" && i + 1 < argc) {
            const std::string value = argv[++i];
            const auto colon = value.find(':');
            unsigned time = 0, key = 0;
            if (colon == std::string::npos) {
                usage();
                return 2;
            }
            const auto a = std::from_chars(value.data(), value.data() + colon, time);
            const auto b =
                std::from_chars(value.data() + colon + 1, value.data() + value.size(), key, 16);
            if (a.ec != std::errc{} || a.ptr != value.data() + colon || b.ec != std::errc{} ||
                b.ptr != value.data() + value.size() || time > 60000 || key > 65535) {
                usage();
                return 2;
            }
            scheduled_keys.emplace(time, key);
        } else if (option == "--headless")
            interactive = false;
        else if (option == "--trace-blocks" && i + 1 < argc)
            block_trace_path = argv[++i];
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
        } else if (option == "--debug-dir" && i + 1 < argc) {
            debug_directory = argv[++i];
            if (debug_directory.empty()) {
                usage();
                return 2;
            }
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
    if (!entry_addresses.empty() && research_path.empty()) {
        usage();
        return 2;
    }
    try {
        std::ifstream file(argv[1], std::ios::binary);
        if (!file)
            throw std::runtime_error("cannot open executable");
        std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
        auto image = gateway::load_mz(bytes);
        State state;
        state.trace_video = trace_video;
        state.probe_adlib = probe_adlib;
        state.object_flags_mode = object_flags_mode;
        state.object_state_services = object_state_services;
        if (!research_path.empty()) {
            state.research_trace.open(research_path);
            if (!state.research_trace)
                throw std::runtime_error("cannot open research trace");
        }
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
        // Non-IRET entry lets DOS applications detect the HLE mouse driver.
        vector(0x33, 0x300);
        const std::uint8_t mouse_code[]{0xcd, 0x33, 0xcf};
        std::copy(std::begin(mouse_code), std::end(mouse_code), image.memory.begin() + 0xf0300);
        if (probe_adlib) {
            // SOUND.COM's installation check looks for this 24-byte block
            // immediately before the INT 65h handler entry point.
            std::array<std::uint8_t, 24> signature{};
            signature[0] = 0x51;
            signature[1] = 0x01;
            std::copy_n("SOUND-DRIVER-AD-LIB", 19, signature.begin() + 2);
            signature[21] = signature[22] = 1;
            std::copy(signature.begin(), signature.end(), image.memory.begin() + 0xf03e8);
            const std::uint8_t driver_code[]{0xcd, 0x65, 0xcf};
            std::copy(std::begin(driver_code), std::end(driver_code),
                      image.memory.begin() + 0xf0400);
            vector(0x65, 0x400);
            std::cerr << "AdLib SOUND.COM interface enabled: timed OPL2 music events\n";
        }
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
        if (interactive) {
            state.debug_scene = std::make_unique<gateway::DebugSceneTracker>();
            try {
                state.debug_object_dat = state.drive->read_all("OBJECT.DAT");
                // Validate the resource, but do NOT use its sequential string
                // indexes as RGN labels: RGN words are engine object IDs.
                gateway::parse_object_names(state.debug_object_dat);
            } catch (const gateway::DosIoError &) {
                // Other Legend releases need not have Gateway's OBJECT.DAT.
            } catch (const std::runtime_error &) {
                // An unknown object-name variant cannot block startup.
            }
        }
        auto font_prefix = std::filesystem::path(argv[1]).stem().string();
        std::transform(font_prefix.begin(), font_prefix.end(), font_prefix.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        if (font_prefix.ends_with("MAIN"))
            font_prefix.resize(font_prefix.size() - 4);
        for (const auto suffix : {"_000.FNT", "_100.FNT", "_200.FNT"}) {
            const auto name = font_prefix + suffix;
            std::vector<std::uint8_t> font;
            try {
                font = state.drive->read_all(name);
            } catch (const gateway::DosIoError &e) {
                if (e.code == 2)
                    continue;
                throw;
            }
            if (font.size() < 10 || font[0] != 1 || font[1] != 1 || font[2] != 1 ||
                font[3] != 0x21 || font[4] != 0x7f || !font[6] || font[6] > 16 || font[7] != 8 ||
                font[8] != 0 || font[9] != 0 || font.size() != 10u + 95u * font[6])
                throw std::runtime_error("unexpected BIOS fallback font layout: " + name);
            const unsigned height = font[6];
            auto &glyphs = state.bios_fonts[height];
            for (unsigned c = 0x21; c <= 0x7f; ++c)
                std::copy_n(font.begin() + 10 + (c - 0x21) * height, height, glyphs[c].begin());
            std::cerr << "BIOS fallback font: " << name << " (8x" << std::dec << height << ")\n";
        }
        if (state.bios_fonts.empty()) {
            auto &glyphs = state.bios_fonts[8];
            for (unsigned c = 0; c < 128; ++c)
                for (unsigned row = 0; row < 8; ++row) {
                    // font8x8 stores the leftmost pixel in bit 0; VGA uses bit 7.
                    const auto bits = font8x8_basic[c][row];
                    for (unsigned bit = 0; bit < 8; ++bit)
                        glyphs[c][row] |= ((bits >> bit) & 1) << (7 - bit);
                }
            std::cerr << "BIOS fallback font: built-in ASCII (8x8)\n";
        }
        // Expose the same fallback glyphs to guests using INT 10h/1130h.
        // Each segment has space for 256 glyphs of the specified height.
        for (const auto [height, segment] :
             {std::pair{8u, 0xf400u}, {14u, 0xf500u}, {16u, 0xf600u}}) {
            const auto source = state.font_for(height);
            std::vector<std::uint8_t> table(256 * height);
            for (unsigned c = 0; c < 256; ++c)
                for (unsigned row = 0; row < height; ++row)
                    table[c * height + row] =
                        source->second[c][row * source->first / height];
            check(uc_mem_write(engine.uc, segment * 16, table.data(), table.size()));
        }
        if (const auto font = state.font_for(16); font != state.bios_fonts.end())
            for (unsigned c = 0x21; c <= 0x7f; ++c)
                state.vga.text_font(c, std::span(font->second[c]).first(font->first));
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
        uc_hook block_hook;
        if (!block_trace_path.empty())
            check(uc_hook_add(engine.uc, &block_hook, UC_HOOK_BLOCK,
                              reinterpret_cast<void *>(trace_block), &state, 1, 0));
        std::vector<uc_hook> entry_hooks(entry_addresses.size());
        for (std::size_t i = 0; i < entry_addresses.size(); ++i)
            check(uc_hook_add(engine.uc, &entry_hooks[i], UC_HOOK_CODE,
                              reinterpret_cast<void *>(trace_entry), &state, entry_addresses[i],
                              entry_addresses[i]));
        std::array<uc_hook, 10> object_hooks{};
        if (!object_flags_mode.empty()) {
            const auto count = object_state_services ? object_services.size() : 1;
            for (std::size_t i = 0; i < count; ++i) {
                check(uc_hook_add(engine.uc, &object_hooks[i * 2], UC_HOOK_CODE,
                                  reinterpret_cast<void *>(object_flags_hook), &state,
                                  object_services[i].entry, object_services[i].entry));
                if (object_flags_mode == "verify")
                    check(uc_hook_add(engine.uc, &object_hooks[i * 2 + 1], UC_HOOK_CODE,
                                      reinterpret_cast<void *>(object_flags_hook), &state,
                                      object_services[i].exit, object_services[i].exit));
            }
        }
        uc_hook overlay_hook, overlay_host_hook;
        if (verify_overlays)
            check(uc_hook_add(engine.uc, &overlay_hook, UC_HOOK_CODE,
                              reinterpret_cast<void *>(rtlink_overlay_hook), &state, 0x2c687,
                              0x2c687));
        if (hle_overlays)
            check(uc_hook_add(engine.uc, &overlay_host_hook, UC_HOOK_CODE,
                              reinterpret_cast<void *>(rtlink_overlay_hook), &state, 0x2c516,
                              0x2c516));
        uc_hook video_hook;
        check(uc_hook_add(engine.uc, &video_hook, UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE,
                          reinterpret_cast<void *>(video_memory), &state, 0xa0000, 0xaffff));
#ifdef GATEWAY_SDL
        std::unique_ptr<gateway::SdlHost> host;
        gateway::DebugOverlayData debug_overlay;
        unsigned debug_dump_sequence = 0;
        unsigned last_debug_inspection = 0;
        if (interactive)
            host = std::make_unique<gateway::SdlHost>(
                gateway::DisplayAspect{aspect_width, aspect_height});
#endif
        bool requested_stop = false;
        uc_err status = UC_ERR_OK;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        const auto started = std::chrono::steady_clock::now();
        std::uint64_t elapsed_us = 0;
        std::uint64_t delivered_irq0 = 0;
        while (true) {
            state.elapsed_ms = elapsed_us / 1000;
            while (!scheduled_keys.empty() && scheduled_keys.begin()->first <= state.elapsed_ms) {
                const auto key = scheduled_keys.begin()->second;
                state.keys.push_back(key);
                if (state.research_trace.is_open())
                    state.research_trace << "key ms=" << std::dec << state.elapsed_ms << std::hex
                                         << " value=" << key << '\n';
                scheduled_keys.erase(scheduled_keys.begin());
            }
#ifdef GATEWAY_SDL
            // Poll once per emulated millisecond, not at every ~60-us PIT edge.
            if (host && elapsed_us % 1000 == 0) {
                gateway::DebugActions actions;
                if (!host->poll(state.keys, state.mouse, state.vga, &actions)) {
                    requested_stop = true;
                    break;
                }
                if (state.debug_scene) {
                    if (actions.toggle_auto_follow)
                        state.debug_scene->auto_follow(!state.debug_scene->auto_follow());
                    if (actions.scene_step)
                        state.debug_scene->step(actions.scene_step);
                }
                if (actions.dump_state) {
                    try {
                        const auto directory = !debug_directory.empty()
                                                   ? debug_directory
                                                   : !state.drive->state_directory().empty()
                                                         ? state.drive->state_directory() / "debug"
                                                         : std::filesystem::path("build/debug");
                        std::filesystem::create_directories(directory);
                        const auto stem = "gateway-" + std::to_string(state.elapsed_ms) + "-" +
                                          std::to_string(++debug_dump_sequence);
                        const auto prefix = (directory / stem).string();
                        dump_guest(engine.uc, state, prefix);
                        state.debug_status = "Saved " + stem;
                        std::cerr << "Gateway debug dump: " << prefix << ".{regs,bin,ppm,"
                                     "regions.tsv,debug.txt[,objects.tsv]}\n";
                    } catch (const std::exception &error) {
                        state.debug_status = error.what();
                        std::cerr << "Gateway debug dump failed: " << error.what() << '\n';
                    }
                }
            }
#endif
            const auto pc =
                std::size_t(reg(engine.uc, UC_X86_REG_CS)) * 16 + reg(engine.uc, UC_X86_REG_IP);
            // A 72-count PIT divisor (used by Spellcasting 101's title sound)
            // expires every ~60 us. Run guest instructions between those IRQs;
            // advancing the PIT in 1-ms chunks loses most of the timer edges.
            const auto slice_us = std::min<std::uint64_t>(
                1000 - elapsed_us % 1000, state.pit.microseconds_until_irq());
            const auto instructions = std::max<std::uint64_t>(1, 5000 * slice_us / 1000);
            status = uc_emu_start(engine.uc, pc, 0x100000, 100000, instructions);
            if (state.overlay_resume && state.failure.empty() && status == UC_ERR_OK) {
                state.overlay_resume = false;
                set(engine.uc, UC_X86_REG_CS, 0x2c24);
                set(engine.uc, UC_X86_REG_IP, 0x447);
                continue;
            }
            if (state.hle_return && state.failure.empty() && status == UC_ERR_OK) {
                const auto result = *state.hle_return;
                state.hle_return.reset();
                set(engine.uc, UC_X86_REG_AX, result.value);
                set(engine.uc, UC_X86_REG_SP, result.sp);
                set(engine.uc, UC_X86_REG_CS, result.cs);
                set(engine.uc, UC_X86_REG_IP, result.ip);
                continue;
            }
            if (state.breakpoint_hit) {
                requested_stop = true;
                break;
            }
            if (status != UC_ERR_OK || state.exited || !state.failure.empty())
                break;
            state.clock.advance(slice_us);
            state.pit.advance(slice_us);
            state.opl.advance(slice_us);
            if (probe_adlib) {
                const auto notes_before = state.adlib.played_notes();
                state.adlib.advance(slice_us);
                if (state.research_trace.is_open() &&
                    state.adlib.played_notes() != notes_before)
                    state.research_trace << "adlib-play ms=" << std::dec << elapsed_us / 1000
                                         << " notes=" << state.adlib.played_notes()
                                         << " pending=" << state.adlib.pending_events() << '\n';
            }
            if (state.pit.pending() && !(state.pic_mask & 1) && !state.irq_in_service &&
                (reg(engine.uc, UC_X86_REG_FLAGS) & 0x200)) {
                state.pit.acknowledge();
                state.irq_in_service = true;
                if (!dispatch(engine.uc, 8))
                    throw std::runtime_error("missing IRQ0 vector");
                ++delivered_irq0;
            }
            // Games such as Spellcasting 101 install a raw IRQ1 handler and
            // read keyboard scan codes from port 60h instead of INT 16h.
            // Keep ordinary BIOS input queued when no IRQ1 handler exists.
            std::uint16_t keyboard_vector[2]{};
            check(uc_mem_read(engine.uc, 9 * 4, keyboard_vector, sizeof(keyboard_vector)));
            if (!state.keyboard_irq_pending && !state.keys.empty() &&
                (keyboard_vector[0] || keyboard_vector[1])) {
                state.keyboard_data = state.keys.front() >> 8;
                state.keys.pop_front();
                state.keyboard_irq_pending = true;
            }
            if (state.keyboard_irq_pending && !(state.pic_mask & 2) &&
                !state.irq_in_service && (reg(engine.uc, UC_X86_REG_FLAGS) & 0x200)) {
                state.irq_in_service = true;
                if (!dispatch(engine.uc, 9))
                    throw std::runtime_error("missing IRQ1 vector");
                state.keyboard_irq_pending = false;
            }
            elapsed_us += slice_us;
            if (trace_timing && elapsed_us % 1000000 == 0) {
                const auto wall_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now() - started)
                                         .count();
                std::cerr << std::dec << "TIMING guest_ms=" << elapsed_us / 1000
                          << " wall_ms=" << wall_ms << " speed=" << std::fixed
                          << std::setprecision(2)
                          << (wall_ms ? double(elapsed_us) / (wall_ms * 1000.0) : 0.0)
                          << "x irq0=" << delivered_irq0
                          << " pit_edges=" << state.pit.irq0_edges()
                          << " speaker_writes=" << state.pit.speaker_writes() << '\n';
            }
#ifdef GATEWAY_SDL
            if (host && elapsed_us % 16000 == 0) {
                text_snapshot(engine.uc, state);
                auto samples = state.opl.render(768);
                state.pit.mix_speaker(samples);
                if (host->debug_open() || host->hitboxes_visible()) {
                    if (state.debug_scene)
                        debug_overlay.scene = state.debug_scene->view();
                    debug_overlay.guest_ms = state.elapsed_ms;
                    debug_overlay.dump_message = state.debug_status;
                    char registers[72];
                    std::snprintf(registers, sizeof registers, "CS:IP %04X:%04X DS %04X ms %u",
                                  reg(engine.uc, UC_X86_REG_CS), reg(engine.uc, UC_X86_REG_IP),
                                  reg(engine.uc, UC_X86_REG_DS), state.elapsed_ms);
                    debug_overlay.registers = registers;
                    if ((host->debug_open() || host->hitboxes_visible()) &&
                        (last_debug_inspection == 0 ||
                         state.elapsed_ms - last_debug_inspection >= 250)) {
                        last_debug_inspection = state.elapsed_ms;
                        std::vector<std::uint8_t> memory(1024 * 1024);
                        check(uc_mem_read(engine.uc, 0, memory.data(), memory.size()));
                        auto objects = gateway::inspect_gateway_state(
                            memory, reg(engine.uc, UC_X86_REG_DS));
                        if ((!objects || objects->objects.size() != 734) && state.debug_object_ds)
                            objects = gateway::inspect_gateway_state(memory, *state.debug_object_ds);
                        if (objects && objects->objects.size() == 734) {
                            state.debug_object_ds = objects->ds;
                            if (state.debug_scene)
                                state.debug_scene->set_object_names(
                                    gateway::debug_engine_object_names(*objects,
                                                                       state.debug_object_dat));
                            std::ostringstream summary;
                            summary << "Objects: " << objects->objects.size();
                            if (objects->book_location)
                                summary << "  book@" << std::hex << *objects->book_location;
                            if (objects->drawer_open)
                                summary << " drawer " << (*objects->drawer_open ? "open" : "closed");
                            debug_overlay.object_summary = summary.str();
                        } else {
                            debug_overlay.object_summary = "Objects: not detected at DS";
                        }
                    }
                }
                host->present(state.vga, state.mouse, samples, debug_overlay);
                std::this_thread::sleep_until(started + std::chrono::microseconds(elapsed_us));
            }
#endif
            if (milliseconds && elapsed_us >= std::uint64_t(milliseconds) * 1000) {
                requested_stop = true;
                break;
            }
            if (!interactive && !milliseconds &&
                (elapsed_us >= 2000000 || std::chrono::steady_clock::now() >= deadline))
                break;
        }
        if (!block_trace_path.empty()) {
            std::ofstream trace(block_trace_path);
            const auto begin = state.block_count > state.blocks.size()
                                   ? state.block_count - state.blocks.size()
                                   : 0;
            for (auto i = begin; i < state.block_count; ++i) {
                const auto &block = state.blocks[i % state.blocks.size()];
                trace << std::hex << block.address << " size=" << block.size << " CS=" << block.cs
                      << " SS:SP=" << block.ss << ':' << block.sp << " BP=" << block.bp
                      << " FLAGS=" << block.flags << '\n';
            }
            trace.close();
            if (!trace)
                throw std::runtime_error("cannot write guest block trace");
        }
        if (!dump_prefix.empty()) {
            dump_guest(engine.uc, state, dump_prefix);
        }
        if (state.research_trace.is_open()) {
            state.research_trace.close();
            if (!state.research_trace)
                throw std::runtime_error("cannot write research trace");
        }
        if (!object_flags_mode.empty())
            std::cerr << "Object flag " << object_flags_mode << " calls: " << std::dec
                      << state.object_flags_calls << '\n';
        if (object_state_services)
            for (std::size_t i = 0; i < object_services.size(); ++i)
                std::cerr << "Object service " << object_services[i].name
                          << " calls: " << state.object_service_calls[i] << '\n';
        if (verify_overlays) {
            unsigned total = 0, distinct = 0;
            for (auto loads : state.overlay_loads) {
                total += loads;
                distinct += loads != 0;
            }
            std::cerr << "RTLink verified loads: " << std::dec << total << " sections: " << distinct
                      << '\n';
        }
        if (hle_overlays)
            std::cerr << "RTLink host loads: " << std::dec << state.overlay_host_loads << '\n';
        if (probe_adlib)
            std::cerr << "AdLib interface calls: " << std::dec << state.adlib_calls
                      << ", synthesized notes: " << state.adlib.played_notes()
                      << ", pending events: " << state.adlib.pending_events() << '\n';
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
