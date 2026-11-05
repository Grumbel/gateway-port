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
                gate.skipped() == 0 && !gate.current_instruction(),
            "hook-order RETF callbacks must not consume a follow sample");
    require(gate.observe(0x26a8f).kind == Kind::waiting && gate.skipped() == 1,
            "repeated RETF callbacks must count toward the bounded wait");
    require(gate.observe(0x09000).kind == Kind::waiting && gate.skipped() == 2,
            "intervening interrupt handler is not caller evidence");
    auto tick = gate.observe(0x34123);
    require(tick.kind == Kind::sample && tick.call == 17 && tick.step == 0,
            "actual caller return address starts the bounded window");
    require(gate.current_instruction() && gate.current_instruction()->call == 17 &&
                gate.current_instruction()->step == 0 &&
                gate.current_instruction()->linear == 0x34123,
            "a guest store can be attributed to the currently executed instruction");
    tick = gate.observe(0x34125);
    require(tick.kind == Kind::sample && tick.step == 1, "follow window is sequential");
    tick = gate.observe(0x23210);
    require(tick.kind == Kind::finished && tick.step == 2 && !gate.active() &&
                gate.recorded() == 3 && gate.current_instruction() &&
                gate.current_instruction()->step == 2,
            "window must stop exactly at its instruction budget, including branches");
    require(gate.observe(0x99999).kind == Kind::inactive && !gate.current_instruction(),
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
    require(!gate.active() && !gate.current_instruction(),
            "zero-budget follow windows must remain disabled");
    gate.arm(22, 0x12345, 0x26a8f, 2);
    gate.observe(0x12345);
    gate.clear();
    require(!gate.current_instruction(), "forced abort must clear store attribution");
    gateway::PickerDispatchWindow dispatch;
    require(!dispatch.match(100), "dispatch association is opt-in");
    dispatch.arm(23, 100, 2000);
    require(dispatch.match(100) && dispatch.match(100)->call == 23 &&
                dispatch.match(100)->age_ms == 0,
            "picker dispatch window begins at the native return timestamp");
    require(dispatch.match(2100) && dispatch.match(2100)->age_ms == 2000,
            "inclusive age limit retains the last candidate");
    require(!dispatch.match(2101) && !dispatch.active(),
            "old clicks must not be associated with unrelated later actions");
    dispatch.arm(24, 3000, 2000);
    dispatch.arm(25, 3100, 2000);
    require(dispatch.match(3101) && dispatch.match(3101)->call == 25,
            "a second picker supersedes the previous temporal association");
    dispatch.clear();
    require(!dispatch.match(3110), "new picker entry invalidates old dispatch candidates");
    dispatch.arm(26, 3000, 2000);
    require(!dispatch.match(2999), "time reversal invalidates the association");
    dispatch.arm(27, 100, 0);
    require(!dispatch.match(100), "zero-size dispatch windows remain disabled");
    gateway::PickerInputHistory input;
    require(!input.recent(200, 20, 30, 500), "no guest mouse read is unlinked");
    input.observe({1, 100, 3, -1, 0, 0, 20, 30, 0x1000, 0x550});
    auto linked = input.recent(120, 20, 30, 50);
    require(linked && linked->read.seq == 1 && linked->age_ms == 20 &&
                linked->coordinates_match && linked->read.function == 3,
            "native picker can reference the last real guest position poll");
    input.observe({2, 125, 5, 0, 0, 1, 40, 50, 0x1000, 0x552});
    linked = input.recent(130, 20, 30, 50);
    require(linked && linked->read.seq == 2 && !linked->coordinates_match &&
                linked->read.count == 1,
            "never skip newer mismatching poll to claim an older coordinate match");
    require(!input.recent(201, 40, 50, 50),
            "expired guest read must not be attributed to a later picker");
    require(!input.recent(99, 20, 30, 50), "future guest reads are not evidence");
    for (unsigned i = 3; i <= 80; ++i)
        input.observe({i, i + 200, 3, -1, 0, 0, int(i), int(i), 0x1000, 0x550});
    require(input.recent(280, 80, 80, 50) &&
                input.recent(280, 80, 80, 50)->read.seq == 80,
            "bounded history must retain the newest guest read");
    std::cout << "Gateway bounded picker continuation checks passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
