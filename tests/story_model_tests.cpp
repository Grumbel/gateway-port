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
using gateway::StoryChildCandidate;

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

        // Callback 0x2b's Tanning Room UP branch is a posture reset, not a
        // room transition. Native standing/reclining runs both returned AX=0;
        // only the latter wrote player location 0x029c -> 0x029a.
        StoryModel tanning;
        constexpr gateway::StoryObjectId tanning_room = 0x29a, couch = 0x29c;
        tanning.define(player, tanning_room);
        tanning.define(tanning_room);
        tanning.define(couch, tanning_room);
        auto up = tanning.apply_tanning_up(player);
        if (up.return_ax != 0 || !up.changes.empty() ||
            tanning.location(player) != tanning_room)
            throw std::runtime_error("standing Tanning UP incorrectly changed state");
        expect(tanning.apply_success(StoryAction::Drop, player, player, couch),
               {{StoryMutationKind::SetLocation, player, couch}},
               "tanning fixture did not place player on couch");
        up = tanning.apply_tanning_up(player);
        if (up.return_ax != 0)
            throw std::runtime_error("Tanning UP returned a navigable room ID");
        expect(up.changes, {{StoryMutationKind::SetLocation, player, tanning_room}},
               "reclining Tanning UP failed to get off couch");
        if (tanning.location(player) != tanning_room ||
            !tanning.apply_tanning_up(player).changes.empty())
            throw std::runtime_error("repeat Tanning UP was not idempotent");

        // In the original VR Terminal trace, an equipped collar blocks UP
        // while player 0xd3 remains in couch 0x2a1. Removing the collar lets
        // the same event move the player into room 0x29b, still returning 0.
        StoryModel terminal;
        terminal.define(player, 0x2a1);
        terminal.define(0x29b);
        terminal.define(0x2a1, 0x29b);
        up = terminal.apply_vr_terminal_up(player, true);
        if (up.return_ax != 0 || !up.changes.empty() || terminal.location(player) != 0x2a1)
            throw std::runtime_error("equipped VR collar failed to block getting up");
        up = terminal.apply_vr_terminal_up(player, false);
        if (up.return_ax != 0)
            throw std::runtime_error("VR Terminal UP returned a navigable destination");
        expect(up.changes, {{StoryMutationKind::SetLocation, player, 0x29b}},
               "unequipped VR Terminal UP failed to get off couch");
        if (!terminal.apply_vr_terminal_up(player, false).changes.empty() ||
            !terminal.apply_vr_terminal_up(player, true).changes.empty())
            throw std::runtime_error("standing VR Terminal UP changed state");

        // Beach shares callback 0x2b but has a distinct side effect: leaving
        // nested object 0x2b1 clears property 0x0a on glass 0x2b4. Native
        // code calls the flag clear even if the bit was already absent.
        StoryModel beach;
        beach.define(player, 0x2b1);
        beach.define(0x2ae);
        beach.define(0x2b1, 0x2ae);
        beach.define(0x2b4);
        beach.initial_flag(0x2b4, 0x0a);
        up = beach.apply_beach_up(player);
        if (up.return_ax != 0)
            throw std::runtime_error("Beach UP returned a navigable destination");
        expect(up.changes, {{StoryMutationKind::SetLocation, player, 0x2ae},
                            {StoryMutationKind::ClearFlag, 0x2b4, 0x0a}},
               "Beach get-up missed its player move or glass-property clear");
        if (beach.flag(0x2b4, 0x0a) || beach.location(player) != 0x2ae ||
            !beach.apply_beach_up(player).changes.empty())
            throw std::runtime_error("repeat Beach UP unexpectedly mutated state");

        StoryModel beach_without_glass_bit;
        beach_without_glass_bit.define(player, 0x2b1);
        beach_without_glass_bit.define(0x2b4);
        expect(beach_without_glass_bit.apply_beach_up(player).changes,
               {{StoryMutationKind::SetLocation, player, 0x2ae}},
               "absent Beach glass bit produced a spurious flag mutation");

        // Native child-slot eligibility is distinct from relation traversal
        // and from the action handlers' OPEN/CLOSE guards.
        if (model.can_enumerate_child_slot(drawer, 2, 0) ||
            model.can_enumerate_child_slot(drawer, 2, 1) ||
            !model.can_enumerate_child_slot(drawer, 2, 2) ||
            model.can_enumerate_child_slot(drawer, 2, 3) ||
            !model.can_enumerate_child_slot(drawer, 1, 99) ||
            !model.can_enumerate_child_slot(drawer, 7, 99))
            throw std::runtime_error("initial parser child-slot predicates differ");
        model.initial_flag(drawer, 0x13);
        if (model.can_enumerate_child_slot(drawer, 2, 0))
            throw std::runtime_error("slot zero ignored closed and opaque flags");
        model.initial_flag(drawer, 0x1f);
        if (!model.can_enumerate_child_slot(drawer, 2, 0))
            throw std::runtime_error("slot zero ignored alternate openness flag");
        model.initial_flag(drawer, 0x1f, false);
        model.initial_flag(drawer, 0x1c);
        if (!model.can_enumerate_child_slot(drawer, 2, 1))
            throw std::runtime_error("slot one ignored property 0x1c");
        model.initial_flag(drawer, 0x1c, false);
        // The recorded 7864–37404 ms candidate trace distinguishes two
        // independent facts: a slot can be gated off while its child pointer
        // still contains the book, and an open drawer can have an empty slot
        // after TAKE. A subsequent watched native replay established that
        // the actual drawer type is 2 and its child slot zero is at +0x14.
        // Supply observed child words; do not invent pointer mutations.
        constexpr std::array<gateway::StoryObjectId, 3> with_book{book, 0, 0};
        constexpr std::array<gateway::StoryObjectId, 3> without_book{0, 0, 0};
        if (!model.eligible_child_slots(drawer, 2, with_book, true).empty() ||
            model.eligible_child_slots(drawer, 2, with_book, false) !=
                std::vector<StoryChildCandidate>{{0, book}})
            throw std::runtime_error("closed drawer candidate-slot gating differs");
        bool invalid_child_type_rejected = false;
        try {
            (void)model.can_enumerate_child_slot(drawer, 0, 0);
        } catch (const std::invalid_argument &) {
            invalid_child_type_rejected = true;
        }
        if (!invalid_child_type_rejected)
            throw std::runtime_error("invalid native child type was accepted");

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
        if (!model.can_enumerate_child_slot(drawer, 2, 0))
            throw std::runtime_error("OPEN did not enable restricted child slot zero");
        if (model.eligible_child_slots(drawer, 2, with_book, true) !=
            std::vector<StoryChildCandidate>{{0, book}})
            throw std::runtime_error("open drawer failed to enumerate its child");
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
        if (model.can_enumerate_child_slot(drawer, 2, 0))
            throw std::runtime_error("CLOSE did not disable restricted child slot zero");
        if (!model.eligible_child_slots(drawer, 2, with_book, true).empty())
            throw std::runtime_error("closed drawer still exposed a child slot");
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
            (void)model.evaluate_carry_precheck(StoryAction::Open, drawer, player);
        } catch (const std::invalid_argument &) {
            rejected_non_carry = true;
        }
        if (!rejected_non_carry)
            throw std::runtime_error("carry precheck accepted an unrelated action");
        expect(model.observe(book_scene), {}, "already-known book changed on reopen");
        if (model.evaluate_carry_precheck(StoryAction::Take, book, player) !=
                StoryCarryPrecheck::Continue ||
            model.evaluate_carry_precheck(StoryAction::Drop, book, player) !=
                StoryCarryPrecheck::NotHolding)
            throw std::runtime_error("unheld book's selector prechecks differ");
        expect(model.apply_success(StoryAction::Take, book, player, room),
               {{StoryMutationKind::SetLocation, book, player},
                {StoryMutationKind::SetFlag, book, 0x02}},
               "initial TAKE effects differ from native trace");
        if (!model.can_enumerate_child_slot(drawer, 2, 0) ||
            !model.eligible_child_slots(drawer, 2, without_book, true).empty())
            throw std::runtime_error("post-TAKE empty child slot confused with gate denial");
        constexpr std::array<gateway::StoryObjectId, 3> hole_then_child{0, 0, book};
        if (model.eligible_child_slots(drawer, 2, hole_then_child, true) !=
            std::vector<StoryChildCandidate>{{2, book}})
            throw std::runtime_error("zero child wrongly stopped later slot enumeration");
        if (model.evaluate_carry_precheck(StoryAction::Take, book, player) !=
                StoryCarryPrecheck::AlreadyHolding ||
            model.evaluate_carry_precheck(StoryAction::Drop, book, player) !=
                StoryCarryPrecheck::Continue)
            throw std::runtime_error("held book's selector prechecks differ");
        expect(model.apply_success(StoryAction::Drop, book, player, room),
               {{StoryMutationKind::SetLocation, book, room}},
               "DROP did not move the book to the room");
        if (model.evaluate_carry_precheck(StoryAction::Drop, book, player) !=
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

        // DROP calls original 0157:00c5, which recursively follows relation
        // links. TAKE instead compares the target's *direct* location only.
        constexpr gateway::StoryObjectId pouch = 0x202, coin = 0x203;
        model.define(pouch, player);
        model.define(coin, pouch);
        if (!model.follows_relation_path(coin, player) ||
            model.follows_relation_path(player, player) ||
            model.evaluate_carry_precheck(StoryAction::Drop, coin, player) !=
                StoryCarryPrecheck::Continue ||
            model.evaluate_carry_precheck(StoryAction::Take, coin, player) !=
                StoryCarryPrecheck::Continue)
            throw std::runtime_error("nested-object path or TAKE/DROP distinction incorrect");
        expect(model.apply_success(StoryAction::Drop, pouch, player, room),
               {{StoryMutationKind::SetLocation, pouch, room}},
               "moving a container did not update its primary relation");
        if (model.follows_relation_path(coin, player) ||
            model.evaluate_carry_precheck(StoryAction::Drop, coin, player) !=
                StoryCarryPrecheck::NotHolding)
            throw std::runtime_error("moving a container left nested object held");

        // Type-6 records can hold several ordered relations. The original
        // loop stops on the *first* zero, even when later bytes are nonzero.
        constexpr gateway::StoryObjectId multi = 0x204, dead_end = 0x205;
        constexpr gateway::StoryObjectId branch = 0x209;
        model.define(multi, dead_end);
        model.define(dead_end, room);
        model.define(branch);
        const std::array relations{branch, player};
        model.set_relation_tail(multi, relations);
        if (!model.follows_relation_path(multi, player))
            throw std::runtime_error("secondary type-6 relation was not traversed");
        expect(model.apply_success(StoryAction::Take, multi, player, room),
               {{StoryMutationKind::SetLocation, multi, player},
                {StoryMutationKind::SetFlag, multi, 0x02}},
               "moving a type-6 object did not update its primary link");
        if (!model.follows_relation_path(multi, branch))
            throw std::runtime_error("moving type-6 object clobbered its other relations");
        const std::array zero_then_player{gateway::StoryObjectId{0}, player};
        model.set_relation_tail(multi, zero_then_player);
        if (model.follows_relation_path(multi, 0x999))
            throw std::runtime_error("unknown relationship target was reachable");
        // Move the primary link away from the player so a zero early exit
        // cannot be accidentally masked by an immediate primary match.
        expect(model.apply_success(StoryAction::Drop, multi, player, room),
               {{StoryMutationKind::SetLocation, multi, room}},
               "moving multi-link object back to room failed");
        if (model.follows_relation_path(multi, player))
            throw std::runtime_error("zero relation slot did not terminate the native list");

        // Native assembly has no cycle guard; the host model must terminate
        // while preserving one-or-more-link (non-reflexive) semantics.
        constexpr gateway::StoryObjectId a = 0x206, b = 0x207;
        model.define(a, b);
        model.define(b, a);
        if (model.follows_relation_path(a, room) || !model.follows_relation_path(a, a))
            throw std::runtime_error("cyclic relationship traversal was incorrect");
        constexpr gateway::StoryObjectId absent = 0x208;
        model.define(absent, 0x7fff); // Unknown destination stops traversal.
        if (model.follows_relation_path(absent, player))
            throw std::runtime_error("undefined relation destination was followed");

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
