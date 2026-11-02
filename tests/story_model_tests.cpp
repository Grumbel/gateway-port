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
using gateway::StoryStudyMatch;
using gateway::StoryStudyResponsePath;
using gateway::StoryStudyResponseState;
using gateway::StoryVrReturnInputs;
using gateway::StoryVrReturnPath;

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

        // Original section-48 callback 0x14 is used for both event 1 and
        // kind-4 room exit event 0x0f. Native Casino replays prove that
        // the former returns Lounge 0x0f6 without performing navigation,
        // while the latter requires the pin directly attached to the player.
        StoryModel casino;
        constexpr gateway::StoryObjectId pin = 0x2a9, oak_door = 0x1ad;
        casino.define(pin, 0x2a3);
        casino.define(oak_door);
        casino.initial_flag(oak_door, 0x0c);
        auto callback = casino.apply_casino_callback(player, 1);
        if (callback.return_ax != 0x0f6 || !callback.changes.empty() ||
            !casino.flag(oak_door, 0x0c))
            throw std::runtime_error("Casino non-exit event incorrectly gated the Lounge ID");
        callback = casino.apply_casino_callback(player, 0x0f);
        if (callback.return_ax != 0)
            throw std::runtime_error("Casino admitted a player lacking the membership pin");
        expect(callback.changes, {{StoryMutationKind::ClearFlag, oak_door, 0x0c}},
               "Casino pin-refusal failed to clear the heavy oak door flag");
        if (casino.flag(oak_door, 0x0c) ||
            !casino.apply_casino_callback(player, 0x0f).changes.empty())
            throw std::runtime_error("Casino repeated flag clear changed already-clear state");

        StoryModel admitted;
        admitted.define(pin, player);
        admitted.define(oak_door);
        admitted.initial_flag(oak_door, 0x0c);
        callback = admitted.apply_casino_callback(player, 0x0f);
        if (callback.return_ax != 0x0f6 || !callback.changes.empty() ||
            !admitted.flag(oak_door, 0x0c))
            throw std::runtime_error("Casino rejected an actual pin holder or cleared door");

        StoryModel nested_pin;
        nested_pin.define(pin, 0x0300);
        nested_pin.define(0x0300, player); // Indirectly inside a carried container.
        nested_pin.define(oak_door);
        if (nested_pin.apply_casino_callback(player, 0x0f).return_ax != 0)
            throw std::runtime_error("Casino used recursive instead of direct pin relation");

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

        // Study callback 0x2b refuses UP before the native dialogue counter
        // reaches 3 (observed in the unmodified Study replay). Static x86
        // establishes the two signed bounds and common get-up destination.
        for (std::int16_t progress : {-1, 0, 1, 2, 20, 21, 0x7fff}) {
            StoryModel blocked;
            blocked.define(player, 0x2be);
            if (blocked.apply_study_up(player, progress).return_ax != 0 ||
                !blocked.apply_study_up(player, progress).changes.empty() ||
                blocked.location(player) != 0x2be)
                throw std::runtime_error("Study UP ignored a signed counter bound");
        }
        for (std::int16_t progress : {3, 12, 19}) {
            StoryModel permitted;
            permitted.define(player, 0x2be);
            up = permitted.apply_study_up(player, progress);
            if (up.return_ax != 0)
                throw std::runtime_error("Study UP returned a navigable destination");
            expect(up.changes, {{StoryMutationKind::SetLocation, player, 0x2bc}},
                   "Study UP failed to get off couch in counter window");
            if (!permitted.apply_study_up(player, progress).changes.empty())
                throw std::runtime_error("Study UP moved a player already in the room");
        }

        // Callback 0x05 is used for DOWN in both Study and the scale booth.
        // The original 0157:000c tests the type-7 player's one immediate
        // relation slot. Signed Study counter 9–12 locks the desk but does
        // not restrict climbing off the scale in the unrelated booth.
        for (std::int16_t progress : {9, 10, 11, 12}) {
            StoryModel on_desk;
            on_desk.define(player, 0x2bf);
            auto down = on_desk.apply_desk_scale_down(player, 0x2bc, progress);
            if (down.return_ax != 0 || !down.changes.empty() ||
                on_desk.location(player) != 0x2bf)
                throw std::runtime_error("Study desk DOWN ignored locked counter window");
        }
        for (std::int16_t progress : {-32768, -1, 0, 8, 13, 19, 32767}) {
            StoryModel on_desk;
            on_desk.define(player, 0x2bf);
            auto down = on_desk.apply_desk_scale_down(player, 0x2bc, progress);
            if (down.return_ax != 0)
                throw std::runtime_error("Study DOWN returned a destination in AX");
            expect(down.changes, {{StoryMutationKind::SetLocation, player, 0x2bc}},
                   "Study desk DOWN ignored signed outside-window branch");
            if (!on_desk.apply_desk_scale_down(player, 0x2bc, progress).changes.empty())
                throw std::runtime_error("repeat Study DOWN moved a standing player");
        }
        StoryModel on_scale;
        on_scale.define(player, 0x87);
        up = on_scale.apply_desk_scale_down(player, 0x47, 10);
        if (up.return_ax != 0)
            throw std::runtime_error("scale DOWN returned a room destination");
        expect(up.changes, {{StoryMutationKind::SetLocation, player, 0x47}},
               "booth DOWN must ignore the Study-only counter gate");
        StoryModel in_study_room;
        in_study_room.define(player, 0x2bc);
        if (!in_study_room.apply_desk_scale_down(player, 0x2bc, 0).changes.empty())
            throw std::runtime_error("Study DOWN moved a player not on the desk");

        // The Study room's event-1 handler matches a byte-sized parser
        // action and optionally a parsed object before setting :7909. Its
        // records are independent of the response-handler counter update.
        // Each rule below was read from the ORIGINAL [DS:9652] table in an
        // unmodified Stage-20 native RAM snapshot, not inferred from prose.
        constexpr std::array<gateway::StoryStudyActionRule, 20> expected_study{{
            {0xb1, 0}, {0xa4, 0x2be}, {0x47, 0}, {0x52, 0},
            {0x08, 0xd3, 0x55, 0x56, true}, {0x18, 0xe4}, {0x96, 0xe4},
            {0x16, 0x165}, {0x52, 0x2bf, 0x18, 0, true}, {0x94, 0},
            {0x23, 0}, {0x23, 0}, {0x1c, 0, 0x1b, 0, true},
            {0x08, 0x2be, 0x55, 0x56, true}, {0x15, 0x2bf}, {0x1f, 0},
            {0x88, 0xe4}, {0x57, 0x2de}, {0x92, 0x2be, 0x19, 0x18, true},
            {0x84, 0},
        }};
        for (int i = 1; i <= 20; ++i) {
            const auto rule = StoryModel::study_action_rule(static_cast<std::int16_t>(i));
            if (!rule || *rule != expected_study[i - 1])
                throw std::runtime_error("Study stage record differs from original RAM");
            const auto target = rule->target_object ? rule->target_object : 0xffff;
            const auto matches = [&](std::uint8_t action, gateway::StoryObjectId parsed,
                                     gateway::StoryObjectId actor, bool already) {
                return StoryModel::evaluate_study_action(
                    static_cast<std::int16_t>(i), action, parsed, actor, already);
            };
            if (matches(rule->action_code, target, player, false) != StoryStudyMatch::Matched ||
                matches(rule->action_code, target, player, true) != StoryStudyMatch::Ineligible ||
                matches(rule->action_code, target, 0x2ca, false) != StoryStudyMatch::Ineligible ||
                matches(0xff, target, player, false) != StoryStudyMatch::WrongAction)
                throw std::runtime_error("Study matcher misclassified stage action or guard");
            if (rule->target_object &&
                matches(rule->action_code, 0xffff, player, false) !=
                    StoryStudyMatch::WrongObject)
                throw std::runtime_error("Study matcher failed to check parsed object first");
            if (!rule->target_object &&
                matches(rule->action_code, 0, player, false) != StoryStudyMatch::Matched)
                throw std::runtime_error("Study matcher failed its zero-target wildcard");
            if (rule->has_alternatives) {
                if (matches(rule->alternative_code_1, target, player, false) !=
                        StoryStudyMatch::Matched ||
                    matches(rule->alternative_code_2, target, player, false) !=
                        StoryStudyMatch::Matched)
                    throw std::runtime_error("Study alternate action codes were omitted");
            }
        }
        for (std::int16_t progress : {-32768, -1, 0, 21, 32767}) {
            if (StoryModel::study_action_rule(progress) ||
                StoryModel::evaluate_study_action(progress, 0xb1, 0, player, false) !=
                    StoryStudyMatch::Ineligible)
                throw std::runtime_error("Study matcher read beyond its bounded stage table");
        }
        for (int progress : {6, 7, 17}) {
            const auto action = expected_study[progress - 1].action_code;
            if (StoryModel::evaluate_study_action(progress, action, 0xe3, player, false) !=
                StoryStudyMatch::Matched ||
                StoryModel::evaluate_study_action(progress, action, 0xe2, player, false) !=
                StoryStudyMatch::WrongObject)
                throw std::runtime_error("Study hand-0xe3 alias is not equivalent to 0xe4");
        }
        // Stage-9 and stage-13 zero alternatives are not sentinels in x86:
        // the original loop explicitly compares both bytes, even if zero.
        if (StoryModel::evaluate_study_action(9, 0, 0x2bf, player, false) !=
                StoryStudyMatch::Matched ||
            StoryModel::evaluate_study_action(13, 0, 0, player, false) !=
                StoryStudyMatch::Matched ||
            StoryModel::evaluate_study_action(19, 0, 0x2be, player, false) !=
                StoryStudyMatch::WrongAction)
            throw std::runtime_error("Study zero-valued alternative was misinterpreted");

        // The original response function at OVL 0x6ce85 decides prompts,
        // reminders, insistence and accepted progress independently of the
        // room's action matcher. It always returns AX=1; these results are
        // a bounded *pure control-flow* translation, not rendered dialogue.
        StoryStudyResponseState therapy{};
        auto response = StoryModel::evaluate_study_response(therapy, true, false);
        if (!response || response->path != StoryStudyResponsePath::NewPrompt ||
            response->story_token != 0x70af || response->table_stage != 1 ||
            response->calls_auxiliary_handler ||
            response->next != StoryStudyResponseState{1, 1, false, false})
            throw std::runtime_error("Study new-prompt path did not initialize stage 1");
        therapy = response->next;
        response = StoryModel::evaluate_study_response(therapy, true, false);
        if (!response || response->path != StoryStudyResponsePath::RepeatPrompt ||
            response->story_token != 0x70ad || response->table_stage != 1 ||
            response->next != StoryStudyResponseState{1, 1, true, false})
            throw std::runtime_error("Study first repeated prompt failed to set :7908");
        therapy = response->next;
        response = StoryModel::evaluate_study_response(therapy, true, false);
        if (!response || response->path != StoryStudyResponsePath::Insistence ||
            response->story_token != 0x70ae || response->table_stage != 0 ||
            !response->calls_auxiliary_handler || response->next != therapy)
            throw std::runtime_error("Study second repetition skipped native auxiliary path");
        therapy.action_matched = true;
        response = StoryModel::evaluate_study_response(therapy, true, false);
        if (!response || response->path != StoryStudyResponsePath::Accepted ||
            response->story_token != 0x70b1 || response->table_stage != 1 ||
            response->next != StoryStudyResponseState{2, 0, false, false})
            throw std::runtime_error("Study acceptance failed to reset three guest fields");
        // At stages 3 and 13 the response handler's direct-membership
        // predicate marks success *after* UP/DOWN moved the player away.
        for (int stage : {3, 13}) {
            therapy = StoryStudyResponseState{static_cast<std::int16_t>(stage),
                                               static_cast<std::uint16_t>(stage), false, false};
            const bool couch = stage == 3, desk = stage == 13;
            auto still_nested = StoryModel::evaluate_study_response(therapy, couch, desk);
            if (!still_nested || still_nested->path != StoryStudyResponsePath::RepeatPrompt)
                throw std::runtime_error("Study response ignored unchanged couch/desk posture");
            auto after_move = StoryModel::evaluate_study_response(therapy, false, false);
            if (!after_move || after_move->path != StoryStudyResponsePath::Accepted ||
                after_move->table_stage != stage || after_move->next.progress != stage + 1 ||
                after_move->next.action_matched)
                throw std::runtime_error("Study UP/DOWN completion missed native posture check");
        }
        for (int stage = 1; stage <= 19; ++stage) {
            therapy = StoryStudyResponseState{static_cast<std::int16_t>(stage), 0, true, true};
            response = StoryModel::evaluate_study_response(therapy, true, true);
            if (!response || response->path != StoryStudyResponsePath::Accepted ||
                response->table_stage != stage || response->next.progress != stage + 1 ||
                response->next.previous_prompt_stage != 0 ||
                response->next.reminder_shown || response->next.action_matched)
                throw std::runtime_error("Study response stage acceptance skipped reset");
        }
        therapy = StoryStudyResponseState{20, 0, false, true};
        response = StoryModel::evaluate_study_response(therapy, true, true);
        if (!response || response->path != StoryStudyResponsePath::Completion ||
            response->story_token != 0x70b0 || !response->calls_auxiliary_handler ||
            response->next != therapy)
            throw std::runtime_error("Study completion path failed original >19 comparison");
        for (std::int16_t stage : {-32768, -1, 21, 32767}) {
            if (StoryModel::evaluate_study_response({stage, 0, false, true}, true, true))
                throw std::runtime_error("Study response admitted an unbounded native stage");
        }
        response = StoryModel::evaluate_study_response({20, 0, false, false}, true, true);
        if (!response || response->path != StoryStudyResponsePath::NewPrompt ||
            response->story_token != 0x70af || response->table_stage != 20 ||
            response->next.previous_prompt_stage != 20)
            throw std::runtime_error("Study failed to select native terminal SLEEP prompt");

        // VR return is a distinct native helper. The branch priority is
        // technician relation -> Beach pin reward -> raw :7920 result.
        // Both unmodified Study return traces take the no-result branch;
        // earlier unmodified Beach gameplay confirms the reward side effects.
        auto vr = StoryModel::evaluate_vr_return({});
        if (vr.path != StoryVrReturnPath::TechnicianAbsent ||
            vr.text_tokens != std::vector<std::uint16_t>{0x70b8} ||
            vr.move_pin_into_terminal || vr.set_timer_44e4_to_0038)
            throw std::runtime_error("VR return without technician printed extra text");

        StoryVrReturnInputs vr_input{true, false, false, 0, false};
        vr = StoryModel::evaluate_vr_return(vr_input);
        if (vr.path != StoryVrReturnPath::NoResult ||
            vr.text_tokens != std::vector<std::uint16_t>({0x70b8, 0x70be}) ||
            !vr.set_timer_44e4_to_0038 || !vr.call_native_event_002b ||
            vr.move_pin_into_terminal || vr.set_byte_7927)
            throw std::runtime_error("Study return missed native no-result event");

        vr_input.result_word_7920 = 4;
        vr = StoryModel::evaluate_vr_return(vr_input);
        if (vr.path != StoryVrReturnPath::OtherResult ||
            vr.text_tokens != std::vector<std::uint16_t>({0x70b8, 0x70ba, 0x70bc}) ||
            vr.set_byte_7927 || vr.call_native_service_434b)
            throw std::runtime_error("other VR result failed first message");
        vr_input.result_seven_seen_7927 = true;
        vr = StoryModel::evaluate_vr_return(vr_input);
        if (vr.path != StoryVrReturnPath::OtherResult ||
            vr.text_tokens != std::vector<std::uint16_t>({0x70b8, 0x70ba, 0x70bb}) ||
            vr.clear_word_6b1a || vr.set_byte_7927)
            throw std::runtime_error("other VR result incorrectly changed one-time state");

        vr_input.result_seven_seen_7927 = false;
        vr_input.result_word_7920 = 7;
        vr = StoryModel::evaluate_vr_return(vr_input);
        if (vr.path != StoryVrReturnPath::ResultSevenFirst ||
            vr.text_tokens != std::vector<std::uint16_t>({0x70b8, 0x70ba, 0x70bd}) ||
            !vr.call_native_service_434b || !vr.clear_word_6b1a || !vr.set_byte_7927)
            throw std::runtime_error("first result-7 return missed guarded side effects");
        vr_input.result_seven_seen_7927 = true;
        vr = StoryModel::evaluate_vr_return(vr_input);
        if (vr.path != StoryVrReturnPath::ResultSevenRepeated ||
            vr.text_tokens != std::vector<std::uint16_t>({0x70b8, 0x70ba, 0x70bb}) ||
            vr.call_native_service_434b || !vr.clear_word_6b1a || !vr.set_byte_7927)
            throw std::runtime_error("repeated result-7 return retriggered one-time service");

        vr_input.beach_success = true;
        vr_input.pin_directly_with_technician = true;
        vr = StoryModel::evaluate_vr_return(vr_input);
        if (vr.path != StoryVrReturnPath::AwardPin ||
            vr.text_tokens != std::vector<std::uint16_t>({0x70b8, 0x70b9}) ||
            !vr.move_pin_into_terminal || !vr.clear_pin_flag_08 ||
            vr.call_native_service_434b || vr.clear_word_6b1a)
            throw std::runtime_error("Beach reward did not override raw result word");
        vr_input.pin_directly_with_technician = false;
        if (StoryModel::evaluate_vr_return(vr_input).path !=
            StoryVrReturnPath::ResultSevenRepeated)
            throw std::runtime_error("Beach success without pin incorrectly awarded reward");
        vr_input.technician_reaches_terminal = false;
        if (StoryModel::evaluate_vr_return(vr_input).path !=
            StoryVrReturnPath::TechnicianAbsent)
            throw std::runtime_error("VR reward ignored absent technician guard");

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
