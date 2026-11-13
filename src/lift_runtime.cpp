// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "lift_runtime.hpp"

#include <dlfcn.h>

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace gateway {
namespace {

constexpr std::array<int, 14> register_ids{
    UC_X86_REG_AX, UC_X86_REG_CX, UC_X86_REG_DX, UC_X86_REG_BX,   UC_X86_REG_SP,
    UC_X86_REG_BP, UC_X86_REG_SI, UC_X86_REG_DI, UC_X86_REG_ES,   UC_X86_REG_CS,
    UC_X86_REG_SS, UC_X86_REG_DS, UC_X86_REG_IP, UC_X86_REG_FLAGS};
constexpr const char *register_names[14] = {"ax", "cx", "dx", "bx", "sp", "bp", "si",
                                            "di", "es", "cs", "ss", "ds", "ip", "flags"};
constexpr std::size_t conventional = 0xa0000;
// AF is not compared: Unicorn's AF after logic, shift and multiply
// instructions is an implementation detail that MSC code never reads.
constexpr std::uint16_t compared_flags = (GW_FLAGS_ARITH | GW_FLAGS_DF) & ~0x0010u;

void check(uc_err err) {
    if (err != UC_ERR_OK)
        throw std::runtime_error(std::string("unicorn: ") + uc_strerror(err));
}

std::array<std::uint16_t, 14> read_registers(uc_engine *uc) {
    std::array<std::uint16_t, 14> out{};
    for (std::size_t i = 0; i < out.size(); ++i) {
        std::uint64_t v = 0;
        check(uc_reg_read(uc, register_ids[i], &v));
        out[i] = std::uint16_t(v);
    }
    return out;
}

void write_registers(uc_engine *uc, const std::array<std::uint16_t, 14> &r) {
    for (std::size_t i = 0; i < r.size(); ++i) {
        std::uint64_t v = r[i];
        check(uc_reg_write(uc, register_ids[i], &v));
    }
}

gw_cpu to_cpu(const std::array<std::uint16_t, 14> &r) {
    gw_cpu c{};
    c.ax = r[0], c.cx = r[1], c.dx = r[2], c.bx = r[3], c.sp = r[4], c.bp = r[5];
    c.si = r[6], c.di = r[7], c.es = r[8], c.cs = r[9], c.ss = r[10], c.ds = r[11];
    c.ip = r[12], c.flags = r[13];
    return c;
}

std::array<std::uint16_t, 14> from_cpu(const gw_cpu &c) {
    return {c.ax, c.cx, c.dx, c.bx, c.sp, c.bp, c.si, c.di, c.es, c.cs, c.ss, c.ds, c.ip, c.flags};
}

struct Direct {
    uc_engine *uc;
    std::uint64_t written = 0;
};

std::uint8_t direct_read(void *ctx, std::uint32_t linear) {
    std::uint8_t v = 0;
    check(uc_mem_read(static_cast<Direct *>(ctx)->uc, linear, &v, 1));
    return v;
}

void direct_write(void *ctx, std::uint32_t linear, std::uint8_t value) {
    // Lifted story code must not reach video memory or ROM: those writes would
    // bypass the emulator's memory hooks.
    if (linear >= conventional)
        throw std::runtime_error("lifted code wrote outside conventional memory");
    auto &d = *static_cast<Direct *>(ctx);
    check(uc_mem_write(d.uc, linear, &value, 1));
    ++d.written;
}

struct Shadow {
    const std::vector<std::uint8_t> *memory;
    std::unordered_map<std::uint32_t, std::uint8_t> writes;
};

std::uint8_t shadow_read(void *ctx, std::uint32_t linear) {
    auto &s = *static_cast<Shadow *>(ctx);
    if (auto it = s.writes.find(linear); it != s.writes.end())
        return it->second;
    if (linear >= s.memory->size())
        throw std::runtime_error("lifted code read outside conventional memory");
    return (*s.memory)[linear];
}

void shadow_write(void *ctx, std::uint32_t linear, std::uint8_t value) {
    if (linear >= conventional)
        throw std::runtime_error("lifted code wrote outside conventional memory");
    static_cast<Shadow *>(ctx)->writes[linear] = value;
}

std::string hex(std::uint64_t v, int width = 4) {
    std::ostringstream out;
    out << std::hex;
    out.width(width);
    out.fill('0');
    out << v;
    return out.str();
}

} // namespace

LiftRuntime::LiftRuntime(const std::filesystem::path &module, Mode mode, std::uint16_t load_segment)
    : mode_(mode), load_segment_(load_segment) {
    handle_ = dlopen(module.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle_)
        throw std::runtime_error(std::string("cannot load lifted module: ") + dlerror());
    using Entry = const gw_lift_module *(*)();
    auto entry = reinterpret_cast<Entry>(dlsym(handle_, "gw_lift_entry"));
    if (!entry || !(module_ = entry()))
        throw std::runtime_error("lifted module has no gw_lift_entry");
    if (module_->abi_version != GW_LIFT_ABI_VERSION)
        throw std::runtime_error("lifted module ABI version mismatch");
    for (unsigned i = 0; i < module_->function_count; ++i) {
        const auto &f = module_->functions[i];
        const auto cs = std::uint16_t(f.linked_segment + load_segment_);
        for (unsigned r = 0; r < f.resume_count; ++r) {
            const auto linear = std::uint64_t(cs) * 16 + f.resume[r];
            targets_.emplace(linear, Target{&f, cs, f.resume[r]});
        }
    }
    if (targets_.empty())
        throw std::runtime_error("lifted module contains no functions");
    hook_begin_ = targets_.begin()->first;
    hook_end_ = targets_.rbegin()->first;
}

LiftRuntime::~LiftRuntime() {
    if (handle_)
        dlclose(handle_);
}

bool LiftRuntime::fingerprint_ok(uc_engine *uc, const gw_lift_function &f, std::uint16_t cs) {
    std::uint64_t hash = 14695981039346656037ull;
    std::vector<std::uint8_t> bytes;
    for (unsigned r = 0; r < f.range_count; ++r) {
        const auto start = f.ranges[r][0], end = f.ranges[r][1];
        bytes.resize(end - start);
        check(uc_mem_read(uc, std::uint64_t(cs) * 16 + start, bytes.data(), bytes.size()));
        for (unsigned x = 0; x < f.fixup_count; ++x)
            for (unsigned b = 0; b < 2; ++b) {
                const unsigned at = f.fixups[x] + b;
                if (at >= start && at < end)
                    bytes[at - start] = 0;
            }
        for (auto byte : bytes)
            hash = (hash ^ byte) * 1099511628211ull;
    }
    return hash == f.fingerprint;
}

bool LiftRuntime::on_code(uc_engine *uc, std::uint64_t address) {
    if (verifying_ || pending_)
        return false;
    auto [first, last] = targets_.equal_range(address);
    if (first == last)
        return false;
    const auto regs = read_registers(uc);
    for (auto it = first; it != last; ++it) {
        const auto &t = it->second;
        if (regs[9] != t.cs || regs[12] != t.ip)
            continue;
        // Overlay sections share load addresses: only the loaded one matches.
        if (!fingerprint_ok(uc, *t.function, t.cs)) {
            ++fingerprint_misses_[t.function];
            continue;
        }
        pending_ = t;
        pending_registers_ = regs;
        check(uc_emu_stop(uc));
        return true;
    }
    return false;
}

bool LiftRuntime::after_stop(uc_engine *uc) {
    if (!pending_)
        return false;
    const auto target = *pending_;
    pending_.reset();
    if (read_registers(uc) != pending_registers_)
        throw std::runtime_error("emulator ran the hooked instruction before the lifted code at " +
                                 std::string(target.function->identity));
    if (mode_ == Mode::Run)
        run(uc, target);
    else
        verify(uc, target);
    return true;
}

void LiftRuntime::run(uc_engine *uc, const Target &target) {
    auto cpu = to_cpu(pending_registers_);
    Direct direct{uc};
    cpu.ctx = &direct;
    cpu.read8 = direct_read;
    cpu.write8 = direct_write;
    const auto rc = target.function->run(&cpu);
    if (rc == GW_LIFT_BAD_ENTRY)
        throw std::runtime_error("lifted function rejected its resume point");
    if (rc == GW_LIFT_EMULATE)
        ++emulated_;
    ++steps_[target.function];
    written_bytes_ += direct.written;
    write_registers(uc, from_cpu(cpu));
    if (trace_)
        *trace_ << "lift-run " << target.function->identity << " ip=" << hex(target.ip)
                << " exit=" << hex(cpu.cs) << ':' << hex(cpu.ip) << " rc=" << rc << '\n';
}

void LiftRuntime::verify(uc_engine *uc, const Target &target) {
    std::vector<std::uint8_t> before(conventional), after(conventional);
    check(uc_mem_read(uc, 0, before.data(), before.size()));

    auto cpu = to_cpu(pending_registers_);
    Shadow shadow{&before, {}};
    cpu.ctx = &shadow;
    cpu.read8 = shadow_read;
    cpu.write8 = shadow_write;
    const auto rc = target.function->run(&cpu);
    if (rc == GW_LIFT_BAD_ENTRY)
        throw std::runtime_error("lifted function rejected its resume point");
    const auto lifted = from_cpu(cpu);

    // Run the original code from the same state to the lifted exit.
    const auto begin = std::uint64_t(target.cs) * 16 + target.ip;
    const auto until = std::uint64_t(cpu.cs) * 16 + cpu.ip;
    verifying_ = true;
    // Unicorn checks ``until`` only while translating: drop a cached block
    // there first, and again afterwards so the stop does not stay compiled in.
    check(uc_ctl_remove_cache(uc, until, until + 1));
    const auto status = uc_emu_start(uc, begin, until, 0, 2000000);
    verifying_ = false;
    check(status);
    check(uc_ctl_remove_cache(uc, until, until + 1));
    const auto native = read_registers(uc);
    check(uc_mem_read(uc, 0, after.data(), after.size()));

    std::ostringstream diff;
    for (std::size_t i = 0; i < native.size(); ++i) {
        const auto mask = i == 13 ? compared_flags : 0xffffu;
        if ((native[i] & mask) != (lifted[i] & mask))
            diff << ' ' << register_names[i] << " native=" << hex(native[i])
                 << " lifted=" << hex(lifted[i]);
    }
    unsigned memory_differences = 0;
    for (std::size_t a = 0; a < conventional; ++a) {
        auto it = shadow.writes.find(std::uint32_t(a));
        const auto value = it == shadow.writes.end() ? before[a] : it->second;
        if (value != after[a] && memory_differences++ < 8)
            diff << " mem[" << hex(a, 5) << "] native=" << hex(after[a], 2)
                 << " lifted=" << hex(value, 2);
    }
    ++steps_[target.function];
    if (rc == GW_LIFT_EMULATE)
        ++emulated_;
    if (trace_)
        *trace_ << "lift-verify " << target.function->identity << " ip=" << hex(target.ip)
                << " exit=" << hex(cpu.cs) << ':' << hex(cpu.ip) << " rc=" << rc
                << " writes=" << shadow.writes.size()
                << (diff.str().empty() ? " ok" : " MISMATCH" + diff.str()) << '\n';
    if (!diff.str().empty())
        throw std::runtime_error("lifted code disagrees with native at " +
                                 std::string(target.function->identity) + " ip=" + hex(target.ip) +
                                 ":" + diff.str());
    ++verified_;
}

std::string LiftRuntime::summary() const {
    std::ostringstream out;
    std::uint64_t steps = 0;
    for (const auto &[f, n] : steps_)
        steps += n;
    out << "lift module=" << module_->name << " mode=" << (mode_ == Mode::Run ? "run" : "verify")
        << " functions=" << module_->function_count << " entered=" << steps_.size()
        << " steps=" << steps << " verified=" << verified_ << " emulated=" << emulated_
        << " written_bytes=" << written_bytes_;
    for (const auto &[f, n] : steps_)
        out << "\n  " << f->identity << " steps=" << n;
    return out.str();
}

} // namespace gateway
