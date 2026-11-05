// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "picker_follow.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *reason) {
    if (!condition)
        throw std::runtime_error(reason);
}
} // namespace

int main() try {
    using Gate = gateway::PickerFollowGate;
    using Kind = Gate::Kind;
    Gate gate;
    require(gate.observe(0x12000).kind == Kind::inactive,
            "disabled tracing must not observe any instruction");
    gate.arm(17, 0x34123, 0x26a8f, 3);
    require(gate.active() && gate.observe(0x26a8f).kind == Kind::waiting &&
                gate.skipped() == 0,
            "hook-order RETF callbacks must not consume a follow sample");
    require(gate.observe(0x26a8f).kind == Kind::waiting && gate.skipped() == 1,
            "repeated RETF callbacks must count toward the bounded wait");
    require(gate.observe(0x09000).kind == Kind::waiting && gate.skipped() == 2,
            "intervening interrupt handler is not caller evidence");
    auto tick = gate.observe(0x34123);
    require(tick.kind == Kind::sample && tick.call == 17 && tick.step == 0,
            "actual caller return address starts the bounded window");
    tick = gate.observe(0x34125);
    require(tick.kind == Kind::sample && tick.step == 1, "follow window is sequential");
    tick = gate.observe(0x23210);
    require(tick.kind == Kind::finished && tick.step == 2 && !gate.active() &&
                gate.recorded() == 3,
            "window must stop exactly at its instruction budget, including branches");
    require(gate.observe(0x99999).kind == Kind::inactive,
            "completed window must never leak instructions from another action");

    gate.arm(18, 0x12345, 0x26a8f, 1);
    for (unsigned i = 0; i < Gate::max_wait; ++i)
        require(gate.observe(0x20202).kind == Kind::waiting,
                "missing caller may wait only a fixed number of instructions");
    tick = gate.observe(0x20202);
    require(tick.kind == Kind::missed_return && tick.call == 18 && !gate.active(),
            "missing return address must produce an explicit failure");

    gate.arm(19, 0x12345, 0x26a8f, 3);
    require(gate.observe(0x12345).kind == Kind::sample && gate.active(),
            "new picker invocation should start a fresh window");
    gate.arm(20, 0x23456, 0x26a8f, 1);
    tick = gate.observe(0x23456);
    require(tick.kind == Kind::finished && tick.call == 20 && tick.step == 0,
            "interrupted window must not mix evidence with a later picker call");
    gate.arm(21, 0x12345, 0x26a8f, 0);
    require(!gate.active(), "zero-budget follow windows must remain disabled");
    std::cout << "Gateway bounded picker continuation checks passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
