// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
//
// Runs instruction-level lifted functions (src/lift_abi.h) inside the
// Unicorn guest, or verifies them against the original code. See
// docs/lifter.md.
#pragma once

#include "lift_abi.h"

#include <unicorn/unicorn.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace gateway {

class LiftRuntime {
  public:
    enum class Mode { Verify, Run };

    // Loads a module built from tools/lift_x86.py output. ``load_segment`` is
    // the paragraph that linked segment 0 runs at (0x1000 in the validated layout).
    LiftRuntime(const std::filesystem::path &module, Mode mode, std::uint16_t load_segment);
    ~LiftRuntime();
    LiftRuntime(const LiftRuntime &) = delete;
    LiftRuntime &operator=(const LiftRuntime &) = delete;

    // Linear range that the code hook has to cover (inclusive).
    std::uint64_t hook_begin() const { return hook_begin_; }
    std::uint64_t hook_end() const { return hook_end_; }

    // From a UC_HOOK_CODE callback. Returns true if it requested a stop.
    bool on_code(uc_engine *uc, std::uint64_t address);
    // From the main loop after uc_emu_start returned. Returns true if a
    // lifted step was handled and the loop should continue emulation.
    bool after_stop(uc_engine *uc);

    void set_trace(std::ostream *trace) { trace_ = trace; }
    std::string summary() const;

  private:
    struct Target {
        const gw_lift_function *function;
        std::uint16_t cs, ip;
    };
    using Registers = std::array<std::uint16_t, 14>; // gw_cpu order: ax..ds, ip, flags

    bool fingerprint_ok(uc_engine *uc, const gw_lift_function &function, std::uint16_t cs);
    void run(uc_engine *uc, const Target &target);
    void verify(uc_engine *uc, const Target &target);

    void *handle_ = nullptr;
    const gw_lift_module *module_ = nullptr;
    Mode mode_;
    std::uint16_t load_segment_;
    std::multimap<std::uint64_t, Target> targets_;
    std::uint64_t hook_begin_ = 0, hook_end_ = 0;
    std::optional<Target> pending_;
    Registers pending_registers_{};
    bool verifying_ = false;
    std::ostream *trace_ = nullptr;
    std::map<const gw_lift_function *, std::uint64_t> steps_, fingerprint_misses_;
    std::uint64_t emulated_ = 0, verified_ = 0, written_bytes_ = 0;
};

} // namespace gateway
