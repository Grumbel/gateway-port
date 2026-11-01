#include "story_model.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

using gateway::StoryAction;
using gateway::StoryModel;
using gateway::StoryMutation;
using gateway::StoryMutationKind;
using gateway::StoryNormalGuard;
using gateway::StoryCarryPrecheck;

void expect(std::vector<StoryMutation> actual, std::initializer_list<StoryMutation> expected,
            const char *message) {
    if (actual != std::vector<StoryMutation>(expected))
        throw std::runtime_error(message);
}

int main() {
    try {
        constexpr gateway::StoryObjectId book = 0x123, drawer = 0x124;
        constexpr gateway::StoryObjectId player = 0xd3, room = 0xee;
        StoryModel model;
        model.define(book, drawer);
        model.define(drawer, 0x125); // The drawer starts inside the desk.
        model.define(player, room);
        model.define(room);
        model.initial_flag(drawer, 0x0e); // Openable, not interpreted by this reducer.
        model.initial_flag(drawer, 0x07);
        model.initial_flag(room, 0x07);
        model.initial_flag(player, 0x07);

        if (model.evaluate_normal_open_close(StoryAction::Open, drawer) !=
            StoryNormalGuard::Allowed)
            throw std::runtime_error("closed, openable drawer rejected by normal OPEN guard");

        // First LOOK in the native trace produces no changed writes.
        expect(model.apply_success(StoryAction::Look, room, player, room), {},
               "LOOK should delegate observed objects to the scene");
        const std::array first_scene{room, drawer};
        expect(model.observe(first_scene), {}, "known room objects changed on LOOK");

        // The repeat replay begins with OPEN and CLOSE.
        expect(model.apply_success(StoryAction::Open, drawer, player, room),
               {{StoryMutationKind::SetFlag, drawer, 0x0c},
                {StoryMutationKind::SetFlag, drawer, 0x02}},
               "initial OPEN changes incorrect flags");
        if (model.evaluate_normal_open_close(StoryAction::Open, drawer) !=
                StoryNormalGuard::AlreadyOpen ||
            model.evaluate_normal_open_close(StoryAction::Close, drawer) !=
                StoryNormalGuard::Allowed)
            throw std::runtime_error("open drawer guard state incorrect");
        const std::array book_scene{book};
        expect(model.observe(book_scene), {{StoryMutationKind::SetFlag, book, 0x07}},
               "newly exposed book did not become known");
        expect(model.apply_success(StoryAction::Close, drawer, player, room),
               {{StoryMutationKind::ClearFlag, drawer, 0x0c}},
               "CLOSE did not clear open bit");
        if (model.evaluate_normal_open_close(StoryAction::Close, drawer) !=
            StoryNormalGuard::AlreadyClosed)
            throw std::runtime_error("repeated CLOSE not classified as already closed");

        // Native repeat run: OPEN again changes only 0x0c, not persistent 0x02/0x07.
        expect(model.apply_success(StoryAction::Open, drawer, player, room),
               {{StoryMutationKind::SetFlag, drawer, 0x0c}},
               "second OPEN modified persistent flags");

        // The native conditionals have different order for OPEN and CLOSE.
        // This test covers contradictions deliberately, so a future
        // simplification cannot accidentally reorder the checks.
        constexpr gateway::StoryObjectId locked = 0x201;
        model.define(locked);
        model.initial_flag(locked, 0x23);
        if (model.evaluate_normal_open_close(StoryAction::Open, locked) !=
                StoryNormalGuard::Blocked ||
            model.evaluate_normal_open_close(StoryAction::Close, locked) !=
                StoryNormalGuard::CannotOperate)
            throw std::runtime_error("OPEN/CLOSE guard precedence changed");
        model.initial_flag(locked, 0x0e);
        if (model.evaluate_normal_open_close(StoryAction::Open, locked) !=
                StoryNormalGuard::Blocked ||
            model.evaluate_normal_open_close(StoryAction::Close, locked) !=
                StoryNormalGuard::Blocked)
            throw std::runtime_error("blocked object passed normal guard");
        model.initial_flag(locked, 0x0c);
        if (model.evaluate_normal_open_close(StoryAction::Open, locked) !=
                StoryNormalGuard::AlreadyOpen ||
            model.evaluate_normal_open_close(StoryAction::Close, locked) !=
                StoryNormalGuard::Blocked)
            throw std::runtime_error("already-open versus blocked precedence changed");
        model.initial_flag(locked, 0x23, false);
        model.initial_flag(locked, 0x0e, false);
        if (model.evaluate_normal_open_close(StoryAction::Open, locked) !=
                StoryNormalGuard::AlreadyOpen ||
            model.evaluate_normal_open_close(StoryAction::Close, locked) !=
                StoryNormalGuard::CannotOperate)
            throw std::runtime_error("missing property 0x0e precedence changed");
        bool rejected_non_open = false;
        try {
            (void)model.evaluate_normal_open_close(StoryAction::Take, drawer);
        } catch (const std::invalid_argument &) {
            rejected_non_open = true;
        }
        if (!rejected_non_open)
            throw std::runtime_error("normal guard accepted an unrelated action");
        bool rejected_non_carry = false;
        try {
            (void)model.evaluate_direct_carry_precheck(StoryAction::Open, drawer, player);
        } catch (const std::invalid_argument &) {
            rejected_non_carry = true;
        }
        if (!rejected_non_carry)
            throw std::runtime_error("carry precheck accepted an unrelated action");
        expect(model.observe(book_scene), {}, "already-known book changed on reopen");
        if (model.evaluate_direct_carry_precheck(StoryAction::Take, book, player) !=
                StoryCarryPrecheck::Continue ||
            model.evaluate_direct_carry_precheck(StoryAction::Drop, book, player) !=
                StoryCarryPrecheck::NotHolding)
            throw std::runtime_error("unheld book's selector prechecks differ");
        expect(model.apply_success(StoryAction::Take, book, player, room),
               {{StoryMutationKind::SetLocation, book, player},
                {StoryMutationKind::SetFlag, book, 0x02}},
               "initial TAKE effects differ from native trace");
        if (model.evaluate_direct_carry_precheck(StoryAction::Take, book, player) !=
                StoryCarryPrecheck::AlreadyHolding ||
            model.evaluate_direct_carry_precheck(StoryAction::Drop, book, player) !=
                StoryCarryPrecheck::Continue)
            throw std::runtime_error("held book's selector prechecks differ");
        expect(model.apply_success(StoryAction::Drop, book, player, room),
               {{StoryMutationKind::SetLocation, book, room}},
               "DROP did not move the book to the room");
        if (model.evaluate_direct_carry_precheck(StoryAction::Drop, book, player) !=
            StoryCarryPrecheck::NotHolding)
            throw std::runtime_error("repeated DROP precheck was not consumed");
        expect(model.apply_success(StoryAction::Take, book, player, room),
               {{StoryMutationKind::SetLocation, book, player}},
               "second TAKE modified persistent flags");
        expect(model.apply_success(StoryAction::Inventory, player, player, room), {},
               "INVENTORY should not change state without newly observed objects");
        const std::array inventory{player, book};
        expect(model.observe(inventory), {}, "INVENTORY rewrote known-object bits");
        if (model.location(book) != player || !model.flag(book, 0x02) ||
            !model.flag(book, 0x07) || !model.flag(drawer, 0x0c) ||
            !model.flag(drawer, 0x02))
            throw std::runtime_error("final modeled object state inconsistent");

        // A separate object demonstrates that TAKE's two clear calls are real
        // operations, merely redundant for the original first-room book.
        constexpr gateway::StoryObjectId other = 0x200;
        model.define(other, room);
        model.initial_flag(other, 0x08);
        model.initial_flag(other, 0x0a);
        expect(model.apply_success(StoryAction::Take, other, player, room),
               {{StoryMutationKind::SetLocation, other, player},
                {StoryMutationKind::SetFlag, other, 0x02},
                {StoryMutationKind::ClearFlag, other, 0x08},
                {StoryMutationKind::ClearFlag, other, 0x0a}},
               "TAKE did not clear existing flags");
        expect(model.apply_success(StoryAction::Take, other, player, room), {},
               "identical TAKE effects were not idempotent");

        bool duplicate_rejected = false;
        try {
            model.define(book, room);
        } catch (const std::invalid_argument &) {
            duplicate_rejected = true;
        }
        if (!duplicate_rejected || model.location(book) != player)
            throw std::runtime_error("duplicate object definition modified state");
        std::cout << "Native first-room action-state model checks passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
