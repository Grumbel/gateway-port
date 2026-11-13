// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
//
// Differential check of lifted functions against Unicorn on random states.
// Usage: lift-check MODULE.so CODE.bin ENTRIES.txt [TRIALS]
// ENTRIES.txt lists the entry IPs (hex) to test; other functions are callees.
// CODE.bin is loaded at each function's linked segment (load segment 0).
// Every function must leave through a far return; the test pushes a
// sentinel return address and compares registers and memory afterwards.

#include "../src/lift_abi.h"

#include <unicorn/unicorn.h>

#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t memory_size = 0x100000;
constexpr std::uint16_t data_segment = 0x3000, stack_segment = 0x4000;
constexpr std::uint16_t sentinel_cs = 0x0000, sentinel_ip = 0x0500;
constexpr std::array<int, 14> ids{UC_X86_REG_AX, UC_X86_REG_CX,   UC_X86_REG_DX, UC_X86_REG_BX,
                                  UC_X86_REG_SP, UC_X86_REG_BP,   UC_X86_REG_SI, UC_X86_REG_DI,
                                  UC_X86_REG_ES, UC_X86_REG_CS,   UC_X86_REG_SS, UC_X86_REG_DS,
                                  UC_X86_REG_IP, UC_X86_REG_FLAGS};
constexpr const char *names[14] = {"ax", "cx", "dx", "bx", "sp", "bp", "si",
                                   "di", "es", "cs", "ss", "ds", "ip", "flags"};

void check(uc_err e) {
    if (e != UC_ERR_OK)
        throw std::runtime_error(uc_strerror(e));
}

std::uint8_t rd(void *ctx, std::uint32_t a) {
    return (*static_cast<std::vector<std::uint8_t> *>(ctx))[a];
}
void wr(void *ctx, std::uint32_t a, std::uint8_t v) {
    (*static_cast<std::vector<std::uint8_t> *>(ctx))[a] = v;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: lift-check MODULE.so CODE.bin ENTRIES.txt [TRIALS]\n");
        return 2;
    }
    const int trials = argc > 4 ? std::stoi(argv[4]) : 300;
    std::set<unsigned> tested;
    {
        std::ifstream list(argv[3]);
        std::string line;
        while (std::getline(list, line))
            if (!line.empty())
                tested.insert(unsigned(std::stoul(line, nullptr, 16)));
    }
    void *so = dlopen(argv[1], RTLD_NOW);
    if (!so) {
        std::fprintf(stderr, "%s\n", dlerror());
        return 1;
    }
    auto entry = reinterpret_cast<const gw_lift_module *(*)()>(dlsym(so, "gw_lift_entry"));
    const auto *module = entry();
    std::ifstream in(argv[2], std::ios::binary);
    const std::vector<std::uint8_t> code{std::istreambuf_iterator<char>(in), {}};
    std::mt19937 rng(12345);
    int failures = 0, skipped = 0;
    for (unsigned fi = 0; fi < module->function_count; ++fi) {
        const auto &f = module->functions[fi];
        if (!tested.count(f.entry))
            continue;
        const std::uint32_t code_base = std::uint32_t(f.linked_segment) * 16;
        int bad = 0;
        for (int t = 0; t < trials && bad < 3; ++t) {
            std::vector<std::uint8_t> memory(memory_size);
            for (std::uint32_t a = data_segment * 16; a < stack_segment * 16 + 0x10000; ++a)
                memory[a] = std::uint8_t(rng());
            std::copy(code.begin(), code.end(), memory.begin() + code_base);
            std::array<std::uint16_t, 14> regs{};
            for (int i = 0; i < 8; ++i)
                regs[i] = std::uint16_t(rng());
            // Small values make equal operands, zero counts and carries likely.
            if (t % 3 == 0)
                for (int i : {0, 1, 2, 3})
                    regs[i] = std::uint16_t(rng() % 4 - 1);
            regs[4] = std::uint16_t(0x8000 + (rng() % 0x4000) * 2);
            regs[8] = regs[11] = data_segment;
            regs[10] = stack_segment;
            regs[9] = f.linked_segment;
            regs[12] = f.entry;
            regs[13] = std::uint16_t(0x0002 | (rng() & 0x0cd5));
            // Sentinel far return address.
            regs[4] = std::uint16_t(regs[4] - 4);
            const std::uint32_t stack = stack_segment * 16 + regs[4];
            memory[stack] = sentinel_ip & 0xff, memory[stack + 1] = sentinel_ip >> 8;
            memory[stack + 2] = sentinel_cs & 0xff, memory[stack + 3] = sentinel_cs >> 8;

            uc_engine *uc;
            check(uc_open(UC_ARCH_X86, UC_MODE_16, &uc));
            check(uc_mem_map(uc, 0, memory_size, UC_PROT_ALL));
            check(uc_mem_write(uc, 0, memory.data(), memory.size()));
            for (int i = 0; i < 14; ++i) {
                std::uint64_t v = regs[i];
                check(uc_reg_write(uc, ids[i], &v));
            }
            const auto err = uc_emu_start(uc, code_base + f.entry,
                                          std::uint64_t(sentinel_cs) * 16 + sentinel_ip, 0, 100000);
            if (err != UC_ERR_OK) {
                // Returning through a clobbered sentinel runs into garbage.
                std::array<std::uint8_t, 4> ret{};
                check(uc_mem_read(uc, stack, ret.data(), ret.size()));
                if (!std::equal(ret.begin(), ret.end(), memory.begin() + stack)) {
                    uc_close(uc);
                    ++skipped;
                    continue;
                }
                std::uint64_t ip = 0;
                uc_reg_read(uc, UC_X86_REG_IP, &ip);
                std::printf("FAIL %s trial %d: unicorn %s at ip=%04x\n", f.identity, t,
                            uc_strerror(err), unsigned(ip));
                uc_close(uc);
                ++bad;
                continue;
            }
            std::array<std::uint16_t, 14> native{};
            for (int i = 0; i < 14; ++i) {
                std::uint64_t v = 0;
                check(uc_reg_read(uc, ids[i], &v));
                native[i] = std::uint16_t(v);
            }
            std::vector<std::uint8_t> native_memory(memory_size);
            check(uc_mem_read(uc, 0, native_memory.data(), native_memory.size()));
            uc_close(uc);
            // A random pointer that overwrote the sentinel makes both sides
            // return into garbage: not a lifting error, skip the trial.
            if (!std::equal(memory.begin() + stack, memory.begin() + stack + 4,
                            native_memory.begin() + stack)) {
                ++skipped;
                continue;
            }

            gw_cpu c{};
            c.ax = regs[0], c.cx = regs[1], c.dx = regs[2], c.bx = regs[3], c.sp = regs[4];
            c.bp = regs[5], c.si = regs[6], c.di = regs[7], c.es = regs[8], c.cs = regs[9];
            c.ss = regs[10], c.ds = regs[11], c.ip = regs[12], c.flags = regs[13];
            c.ctx = &memory;
            c.read8 = rd;
            c.write8 = wr;
            int rc = GW_LIFT_EXIT, steps = 0;
            // Near calls and jumps inside the module re-enter the function.
            while (!(c.cs == sentinel_cs && c.ip == sentinel_ip) && steps++ < 100) {
                const gw_lift_function *g = nullptr;
                for (unsigned k = 0; k < module->function_count && !g; ++k)
                    for (unsigned r = 0; r < module->functions[k].resume_count; ++r)
                        if (module->functions[k].linked_segment == c.cs &&
                            module->functions[k].resume[r] == c.ip)
                            g = &module->functions[k];
                if (!g) {
                    rc = GW_LIFT_BAD_ENTRY;
                    break;
                }
                if ((rc = g->run(&c)) != GW_LIFT_EXIT)
                    break;
            }
            const std::array<std::uint16_t, 14> lifted{c.ax, c.cx, c.dx, c.bx, c.sp, c.bp, c.si,
                                                       c.di, c.es, c.cs, c.ss, c.ds, c.ip, c.flags};
            std::string diff;
            if (rc != GW_LIFT_EXIT)
                diff += " rc=" + std::to_string(rc);
            for (int i = 0; i < 14; ++i) {
                const std::uint16_t mask = i == 13 ? 0x0cc5 : 0xffff; // AF not compared
                if ((native[i] & mask) != (lifted[i] & mask)) {
                    char buf[64];
                    std::snprintf(buf, sizeof buf, " %s native=%04x lifted=%04x", names[i],
                                  native[i], lifted[i]);
                    diff += buf;
                }
            }
            for (std::uint32_t a = 0; a < memory_size; ++a)
                if (memory[a] != native_memory[a]) {
                    char buf[64];
                    std::snprintf(buf, sizeof buf, " mem[%05x] native=%02x lifted=%02x", a,
                                  native_memory[a], memory[a]);
                    diff += buf;
                    break;
                }
            if (!diff.empty()) {
                std::printf("FAIL %s trial %d: in ax=%04x cx=%04x dx=%04x bx=%04x flags=%04x:%s\n",
                            f.identity, t, regs[0], regs[1], regs[2], regs[3], regs[13],
                            diff.c_str());
                ++bad;
            }
        }
        failures += bad != 0;
    }
    std::printf("%zu entries tested (%u functions), %d failing, %d trials skipped\n", tested.size(),
                module->function_count, failures, skipped);
    return failures ? 1 : 0;
}
