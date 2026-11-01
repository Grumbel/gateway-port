#include "story_model.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

using gateway::StoryAction;
using gateway::StoryModel;
using gateway::StoryMutation;
using gateway::StoryMutationKind;

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
        const std::array book_scene{book};
        expect(model.observe(book_scene), {{StoryMutationKind::SetFlag, book, 0x07}},
               "newly exposed book did not become known");
        expect(model.apply_success(StoryAction::Close, drawer, player, room),
               {{StoryMutationKind::ClearFlag, drawer, 0x0c}},
               "CLOSE did not clear open bit");

        // Native repeat run: OPEN again changes only 0x0c, not persistent 0x02/0x07.
        expect(model.apply_success(StoryAction::Open, drawer, player, room),
               {{StoryMutationKind::SetFlag, drawer, 0x0c}},
               "second OPEN modified persistent flags");
        expect(model.observe(book_scene), {}, "already-known book changed on reopen");
        expect(model.apply_success(StoryAction::Take, book, player, room),
               {{StoryMutationKind::SetLocation, book, player},
                {StoryMutationKind::SetFlag, book, 0x02}},
               "initial TAKE effects differ from native trace");
        expect(model.apply_success(StoryAction::Drop, book, player, room),
               {{StoryMutationKind::SetLocation, book, room}},
               "DROP did not move the book to the room");
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
