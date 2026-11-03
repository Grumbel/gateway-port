// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: LicenseRef-Gateway-Derived

#include "story_model.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace gateway {

namespace {

// Native RAM table at [DS:9652]:02a8 + 4*stage (stage zero is unused),
// and the five :02fc alternative entries, extracted from the unmodified
// Study snapshot. The order is the signed :7904 dialogue stage, not an
// ordering of ordinary parser verbs. The stage-20 terminal SLEEP record is
// at :02f8, directly before the alternate table. A zero alternative is a real byte
// comparison in the DOS loop, rather than a missing alternative.
constexpr std::array<StoryStudyActionRule, 20> study_rules{{
    {0xb1, 0x0000},
    {0xa4, 0x02be},
    {0x47, 0x0000},
    {0x52, 0x0000},
    {0x08, 0x00d3, 0x55, 0x56, true},
    {0x18, 0x00e4},
    {0x96, 0x00e4},
    {0x16, 0x0165},
    {0x52, 0x02bf, 0x18, 0x00, true},
    {0x94, 0x0000},
    {0x23, 0x0000},
    {0x23, 0x0000},
    {0x1c, 0x0000, 0x1b, 0x00, true},
    {0x08, 0x02be, 0x55, 0x56, true},
    {0x15, 0x02bf},
    {0x1f, 0x0000},
    {0x88, 0x00e4},
    {0x57, 0x02de},
    {0x92, 0x02be, 0x19, 0x18, true},
    {0x84, 0x0000},  // Terminal instruction from :02f8: sleep.
}};

} // namespace

std::optional<StoryStudyActionRule> StoryModel::study_action_rule(std::int16_t progress) {
    if (progress < 1 || progress > static_cast<std::int16_t>(study_rules.size()))
        return std::nullopt;
    return study_rules[static_cast<std::size_t>(progress - 1)];
}

StoryStudyMatch StoryModel::evaluate_study_action(std::int16_t progress,
                                                  std::uint8_t native_action_code,
                                                  StoryObjectId parsed_object,
                                                  StoryObjectId actor,
                                                  bool action_already_matched) {
    const auto rule = study_action_rule(progress);
    if (actor != 0x0d3 || !rule || action_already_matched)
        return StoryStudyMatch::Ineligible;
    // Object checks precede action-code comparisons in 0x6b6f9–0x6b735.
    // When native expected object is a hand (0xe4), its alias 0xe3 also
    // passes. All other nonzero object IDs require equality.
    if (rule->target_object != 0 && parsed_object != rule->target_object &&
        !(rule->target_object == 0xe4 && parsed_object == 0xe3))
        return StoryStudyMatch::WrongObject;
    if (native_action_code == rule->action_code ||
        (rule->has_alternatives &&
         (native_action_code == rule->alternative_code_1 ||
          native_action_code == rule->alternative_code_2)))
        return StoryStudyMatch::Matched;
    return StoryStudyMatch::WrongAction;
}

std::optional<StoryStudyResponseOutcome> StoryModel::evaluate_study_response(
    StoryStudyResponseState state, bool player_on_couch, bool player_on_desk) {
    // OVL 0x6ce85–0x6d04d: initialize an empty Study counter, then test
    // completion before choosing either a new/repeated prompt or a response.
    // The original prompt table has a valid stage-20 terminal pointer
    // (shared with response entry zero), but per-stage responses only run
    // through 19. The terminal matched branch prints F000:70b0 instead.
    if (state.progress < 0 || state.progress > 20)
        return std::nullopt;
    if (state.progress == 0)
        state.progress = 1;

    // A successful UP at stage 3 or DOWN at stage 13 is detected by the
    // *absence* of the player's old direct parent, after native callback
    // 0x2b or 0x05 has already moved them. The room event-1 action matcher
    // does not have to write :7909 for these two actions.
    if (!state.action_matched &&
        ((state.progress == 3 && !player_on_couch) ||
         (state.progress == 13 && !player_on_desk)))
        state.action_matched = true;

    if (state.action_matched) {
        if (state.progress == 20)
            return StoryStudyResponseOutcome{state, StoryStudyResponsePath::Completion,
                                              0x70b0, 0, true};
        const auto stage = static_cast<std::uint16_t>(state.progress);
        ++state.progress;  // OVL 0x6cff1, BEFORE the response far-pointer call.
        state.action_matched = false;  // OVL 0x6d01c.
        state.previous_prompt_stage = 0;  // OVL 0x6d026.
        state.reminder_shown = false;  // OVL 0x6d031.
        return StoryStudyResponseOutcome{state, StoryStudyResponsePath::Accepted,
                                          0x70b1, stage, false};
    }

    if (state.previous_prompt_stage != static_cast<std::uint16_t>(state.progress)) {
        state.previous_prompt_stage = static_cast<std::uint16_t>(state.progress);
        return StoryStudyResponseOutcome{state, StoryStudyResponsePath::NewPrompt,
                                          0x70af,
                                          static_cast<std::uint16_t>(state.progress), false};
    }
    if (!state.reminder_shown) {
        state.reminder_shown = true;  // OVL 0x6cf4c.
        return StoryStudyResponseOutcome{state, StoryStudyResponsePath::RepeatPrompt,
                                          0x70ad,
                                          static_cast<std::uint16_t>(state.progress), false};
    }
    // OVL 0x6cf55–0x6cf69 calls the same auxiliary native handler as the
    // >19 completion path, but does not clear :7908 or :7906 itself.
    return StoryStudyResponseOutcome{state, StoryStudyResponsePath::Insistence,
                                      0x70ae, 0, true};
}

StoryVrReturnDecision StoryModel::evaluate_vr_return(StoryVrReturnInputs input) {
    // Original OVL:0x6d564 starts every return with F000:70b8. The first
    // test is the recursive 0157:00c5 relation between technician and
    // Terminal, *not* a direct parent comparison.
    StoryVrReturnDecision out{StoryVrReturnPath::TechnicianAbsent, {0x70b8}};
    if (!input.technician_reaches_terminal)
        return out;

    // The Beach reward takes precedence over all :7920 result messages.
    // OVL:0x6d590–0x6d5e0; the pin check is direct 0157:000c.
    if (input.beach_success && input.pin_directly_with_technician) {
        out.path = StoryVrReturnPath::AwardPin;
        out.text_tokens.push_back(0x70b9);
        out.move_pin_into_terminal = true;
        out.clear_pin_flag_08 = true;
        return out;
    }

    if (input.result_word_7920 == 0) {
        // OVL:0x6d67e–0x6d6a6 (both Study replays): set delay word,
        // then call original event service 02e3:002b(0x2b,1).
        out.path = StoryVrReturnPath::NoResult;
        out.text_tokens.push_back(0x70be);
        out.set_timer_44e4_to_0038 = true;
        out.call_native_event_002b = true;
        return out;
    }

    out.text_tokens.push_back(0x70ba);
    if (input.result_word_7920 != 7) {
        out.path = StoryVrReturnPath::OtherResult;
        out.text_tokens.push_back(input.result_seven_seen_7927 ? 0x70bb : 0x70bc);
        return out;
    }

    // OVL:0x6d62e–0x6d67c: result 7 has one-time native side effects.
    // The recorded flag :7927 is used even on the *other-result* message
    // path, but only result 7 actually changes it in this routine.
    out.path = input.result_seven_seen_7927 ? StoryVrReturnPath::ResultSevenRepeated
                                           : StoryVrReturnPath::ResultSevenFirst;
    out.text_tokens.push_back(input.result_seven_seen_7927 ? 0x70bb : 0x70bd);
    out.call_native_service_434b = !input.result_seven_seen_7927;
    out.clear_word_6b1a = true;
    out.set_byte_7927 = true;
    return out;
}

void StoryModel::define(StoryObjectId id, std::optional<StoryObjectId> location) {
    if (!id || !objects_.emplace(id, Object{{}, location, {}}).second)
        throw std::invalid_argument("invalid or duplicate story object");
}

void StoryModel::initial_flag(StoryObjectId id, std::uint16_t property, bool value) {
    if (value)
        objects_.at(id).flags.insert(property);
    else
        objects_.at(id).flags.erase(property);
}

bool StoryModel::flag(StoryObjectId id, std::uint16_t property) const {
    return objects_.at(id).flags.contains(property);
}

std::optional<StoryObjectId> StoryModel::location(StoryObjectId id) const {
    return objects_.at(id).location;
}

void StoryModel::set_relation_tail(StoryObjectId id, std::span<const StoryObjectId> links) {
    objects_.at(id).relation_tail.assign(links.begin(), links.end());
}

bool StoryModel::follows_relation_path(StoryObjectId source, StoryObjectId wanted) const {
    // Each known object is explored at most once. The DOS function at EXE
    // 0x2603 uses recursive DFS without this guard; valid acyclic records
    // have the same Boolean result.
    std::vector<StoryObjectId> pending{source};
    std::set<StoryObjectId> visited{source};
    while (!pending.empty()) {
        const StoryObjectId current = pending.back();
        pending.pop_back();
        const auto found = objects_.find(current);
        if (found == objects_.end())
            continue;
        const Object &object = found->second;
        const auto explore = [&](StoryObjectId link) {
            if (link == wanted)
                return true;
            if (visited.insert(link).second)
                pending.push_back(link);
            return false;
        };
        // A zero entry terminates that object's list in the native helper.
        if (!object.location || !*object.location)
            continue;
        if (explore(*object.location))
            return true;
        for (StoryObjectId link : object.relation_tail) {
            if (!link)
                break;
            if (explore(link))
                return true;
        }
    }
    return false;
}

bool StoryModel::can_enumerate_child_slot(StoryObjectId object, std::uint8_t type,
                                          std::uint16_t slot) const {
    if (type < 1 || type > 8)
        throw std::invalid_argument("child-slot predicate requires a valid native object type");
    // The two kinds bypass all slot and flag checks in the original helper.
    if (type == 1 || type == 7)
        return true;
    switch (slot) {
    case 0:
        return flag(object, 0x13) && (flag(object, 0x0c) || flag(object, 0x1f));
    case 1:
        return flag(object, 0x1c);
    case 2:
        return true;
    default:
        return false;
    }
}

std::vector<StoryChildCandidate> StoryModel::eligible_child_slots(
    StoryObjectId object, std::uint8_t type, std::span<const StoryObjectId> children,
    bool restricted) const {
    if (type < 1 || type > 8)
        throw std::invalid_argument("eligible child slots require a valid native object type");
    if (children.size() > 0x10000)
        throw std::invalid_argument("child slot count exceeds 16-bit native index range");
    std::vector<StoryChildCandidate> result;
    for (std::size_t slot = 0; slot < children.size(); ++slot) {
        // The original iterator first tests the gate if restricted, then
        // fetches child_at(object, slot) and skips zero. It does not treat
        // zero as an end-of-list marker in this loop.
        if ((!restricted || can_enumerate_child_slot(object, type,
                                                     static_cast<std::uint16_t>(slot))) &&
            children[slot])
            result.push_back({static_cast<std::uint16_t>(slot), children[slot]});
    }
    return result;
}

StoryCarryPrecheck StoryModel::evaluate_carry_precheck(StoryAction action,
                                                       StoryObjectId target,
                                                       StoryObjectId player) const {
    switch (action) {
    case StoryAction::Take:
        return location(target) == player ? StoryCarryPrecheck::AlreadyHolding
                                          : StoryCarryPrecheck::Continue;
    case StoryAction::Drop:
        return follows_relation_path(target, player) ? StoryCarryPrecheck::Continue
                                                      : StoryCarryPrecheck::NotHolding;
    default:
        throw std::invalid_argument("carry precheck requires TAKE or DROP");
    }
}

StoryCallbackOutcome StoryModel::apply_tanning_up(StoryObjectId player) {
    StoryCallbackOutcome outcome;
    // OVL 0x694b8–0x69512: native 0157:000c(player, 0x29c), followed by
    // location setter 1c24:13d5(player, current room, 0) on the true branch.
    // Both original paths return AX=0 to the resident exit scanner.
    if (location(player) == 0x29c)
        move(outcome.changes, player, 0x29a);
    return outcome;
}

StoryCallbackOutcome StoryModel::apply_vr_terminal_up(StoryObjectId player,
                                                       bool collar_equipped) {
    StoryCallbackOutcome outcome;
    // OVL 0x6954f–0x6958b: equipment-slot predicate has precedence over
    // the direct player/room relation test. A nonzero answer prints a collar
    // refusal; otherwise a nested player takes the shared get-up path.
    if (!collar_equipped && location(player) != 0x29b)
        move(outcome.changes, player, 0x29b);
    return outcome;
}

StoryCallbackOutcome StoryModel::apply_beach_up(StoryObjectId player) {
    StoryCallbackOutcome outcome;
    // OVL 0x69446–0x694b5: only a player not directly located in the room
    // takes the get-up path. Unlike the shared path, this additionally calls
    // native 0157:0413(glass 0x2b4, property 0x0a) after the player move.
    if (location(player) != 0x2ae) {
        move(outcome.changes, player, 0x2ae);
        set_flag(outcome.changes, 0x2b4, 0x0a, false);
    }
    return outcome;
}

StoryCallbackOutcome StoryModel::apply_study_up(StoryObjectId player,
                                                std::int16_t progress) {
    StoryCallbackOutcome outcome;
    // OVL 0x69515–0x6954d: native direct-parent accessor 0157:0546 and
    // signed jl/jle on [DS:9634]:7904. An out-of-range couch occupant gets
    // a refusal, while an eligible occupant uses the common 0x694cf get-up
    // path. Neither branch supplies a new room ID in AX.
    if (location(player) == 0x2be && progress >= 3 && progress <= 19)
        move(outcome.changes, player, 0x2bc);
    return outcome;
}

StoryCallbackOutcome StoryModel::apply_desk_scale_down(StoryObjectId player,
                                                       StoryObjectId current_room,
                                                       std::int16_t study_progress) {
    StoryCallbackOutcome outcome;
    // OVL 0x6958d–0x69647: event=0x0f; 0157:000c(player, 0x2bf)
    // precedes 0157:000c(player, 0x87). The signed JLE 8 / JGE 13 tests
    // only guard the desk. The original player object is type 7, so its
    // native membership test has one location slot, not recursive ancestry.
    const auto parent = location(player);
    if ((parent == 0x2bf && (study_progress <= 8 || study_progress >= 13)) ||
        parent == 0x87)
        move(outcome.changes, player, current_room);
    return outcome;
}

StoryCallbackOutcome StoryModel::apply_casino_callback(StoryObjectId player,
                                                        std::uint16_t event) {
    // OVL:0x665f9–0x6664e. The native 0157:000c predicate is the one-slot
    // relation accessor for type-8 membership pin 0x2a9, not recursive
    // inventory containment. Event !=0x0f bypasses the predicate and simply
    // returns 0x0f6. The original zero-result branch invokes flag-clear
    // 0157:0413(0x1ad,0x0c), even when the flag was already clear.
    StoryCallbackOutcome outcome{0x0f6, {}};
    if (event == 0x000f && location(0x02a9) != player) {
        outcome.return_ax = 0;
        set_flag(outcome.changes, 0x01ad, 0x000c, false);
    }
    return outcome;
}

std::optional<StoryReceptionistBookOutcome> StoryModel::apply_receptionist_book_exchange(
    StoryObjectId player, std::uint16_t event, std::uint8_t action_code,
    StoryObjectId parsed_object) {
    // OVL 0x56f9f: event 4 -> :e664 action 0x3c -> :055e book 0x123.
    // The original control-flow branch at 0x570c9 selects F000:a83e,
    // uses 1c24:13d5 twice and score service 048e:0a7e(5), then the
    // 048e:0ad6(DataMan) helper. The latter is verified against native
    // end state; this sparse model performs its final location change.
    if (event != 4 || action_code != 0x3c || parsed_object != 0x123)
        return std::nullopt;
    StoryReceptionistBookOutcome result;
    move(result.changes, 0x130, 0x0ff);
    move(result.changes, 0x123, 0);
    move(result.changes, 0x130, player);
    return result;
}

StoryNormalGuard StoryModel::evaluate_normal_open_close(StoryAction action,
                                                         StoryObjectId target) const {
    switch (action) {
    case StoryAction::Open:
        if (flag(target, 0x0c))
            return StoryNormalGuard::AlreadyOpen;
        if (flag(target, 0x23))
            return StoryNormalGuard::Blocked;
        if (!flag(target, 0x0e))
            return StoryNormalGuard::CannotOperate;
        return StoryNormalGuard::Allowed;
    case StoryAction::Close:
        if (!flag(target, 0x0e))
            return StoryNormalGuard::CannotOperate;
        if (flag(target, 0x23))
            return StoryNormalGuard::Blocked;
        if (!flag(target, 0x0c))
            return StoryNormalGuard::AlreadyClosed;
        return StoryNormalGuard::Allowed;
    default:
        throw std::invalid_argument("normal OPEN/CLOSE guard requires OPEN or CLOSE");
    }
}

void StoryModel::set_flag(std::vector<StoryMutation> &changes, StoryObjectId id,
                          std::uint16_t property, bool value) {
    auto &flags = objects_.at(id).flags;
    const bool changed = value ? flags.insert(property).second : flags.erase(property) != 0;
    if (changed)
        changes.push_back({value ? StoryMutationKind::SetFlag : StoryMutationKind::ClearFlag,
                           id, property});
}

void StoryModel::move(std::vector<StoryMutation> &changes, StoryObjectId id,
                      StoryObjectId destination) {
    auto &where = objects_.at(id).location;
    if (where != destination) {
        where = destination;
        changes.push_back({StoryMutationKind::SetLocation, id, destination});
    }
}

std::vector<StoryMutation> StoryModel::apply_success(StoryAction action, StoryObjectId target,
                                                     StoryObjectId player, StoryObjectId room) {
    std::vector<StoryMutation> changes;
    switch (action) {
    case StoryAction::Open:
        set_flag(changes, target, 0x0c, true);
        set_flag(changes, target, 0x02, true);
        break;
    case StoryAction::Close:
        set_flag(changes, target, 0x0c, false);
        break;
    case StoryAction::Take:
        move(changes, target, player);
        set_flag(changes, target, 0x02, true);
        set_flag(changes, target, 0x08, false);
        set_flag(changes, target, 0x0a, false);
        break;
    case StoryAction::Drop:
        move(changes, target, room);
        break;
    case StoryAction::Look:
    case StoryAction::Inventory:
        // Descriptions can call observe() on the objects they expose.
        break;
    }
    return changes;
}

std::vector<StoryMutation> StoryModel::observe(std::span<const StoryObjectId> objects) {
    std::vector<StoryMutation> changes;
    for (const auto object : objects)
        set_flag(changes, object, 0x07, true);
    return changes;
}

void StoryLinkedChildren::define(StoryObjectId object, std::uint16_t child_slots,
                                 StoryObjectId location, std::uint8_t slot,
                                 StoryObjectId next) {
    if (!object || !nodes_.emplace(object, Node{location, next, slot,
                                               std::vector<StoryObjectId>(child_slots, 0)}).second)
        throw std::invalid_argument("invalid or duplicate linked story object");
}

void StoryLinkedChildren::initial_head(StoryObjectId parent, std::uint16_t slot,
                                       StoryObjectId child) {
    nodes_.at(parent).heads.at(slot) = child;
}

StoryObjectId StoryLinkedChildren::head(StoryObjectId parent, std::uint16_t slot) const {
    if (!parent)
        return 0;
    const auto &heads = nodes_.at(parent).heads;
    return slot < heads.size() ? heads[slot] : 0;
}

StoryObjectId StoryLinkedChildren::next(StoryObjectId object) const {
    return nodes_.at(object).next;
}

StoryObjectId StoryLinkedChildren::location(StoryObjectId object) const {
    return nodes_.at(object).location;
}

std::uint8_t StoryLinkedChildren::slot(StoryObjectId object) const {
    return nodes_.at(object).slot;
}

std::vector<StoryObjectId> StoryLinkedChildren::heads(StoryObjectId parent) const {
    return nodes_.at(parent).heads;
}

std::vector<StoryObjectId> StoryLinkedChildren::children(StoryObjectId parent,
                                                        std::uint16_t child_slot) const {
    const auto &record = nodes_.at(parent);
    const auto first = record.heads.at(child_slot);
    std::vector<StoryObjectId> result;
    std::set<StoryObjectId> seen;
    for (auto child = first; child;) {
        if (!seen.insert(child).second)
            throw std::logic_error("cyclic native child-sibling list");
        const auto &node = nodes_.at(child);
        if (node.location != parent || node.slot != child_slot)
            throw std::logic_error("inconsistent native child location or slot");
        result.push_back(child);
        child = node.next;
    }
    return result;
}

void StoryLinkedChildren::move(StoryObjectId object, StoryObjectId destination,
                               std::uint8_t new_slot) {
    auto &entry = nodes_.at(object);
    if (destination) {
        if (destination == object)
            throw std::logic_error("object cannot contain itself");
        (void)nodes_.at(destination).heads.at(new_slot);
    }
    const auto old_parent = entry.location;
    const auto old_slot = entry.slot;
    // Validate the entire old sibling list *before* making any changes. This
    // is a host-side safety choice; DOS code does not do this preflight.
    if (old_parent) {
        const auto siblings = children(old_parent, old_slot);
        if (std::find(siblings.begin(), siblings.end(), object) == siblings.end())
            throw std::logic_error("object absent from its recorded parent chain");
    }
    const auto old_head = head(old_parent, old_slot);
    if (old_head == object) {
        // OVL 0xc869–0xc87e: replace old head with the removed object's next.
        auto &heads = nodes_.at(old_parent).heads;
        heads.at(old_slot) = entry.next;
    } else {
        // OVL 0xc883–0xc8b6: walk the next-sibling chain until the object is
        // found, then splice its successor into its predecessor's next word.
        std::set<StoryObjectId> visited;
        for (auto current = old_head; current;) {
            if (!visited.insert(current).second)
                throw std::logic_error("cyclic native child-sibling list");
            auto &predecessor = nodes_.at(current);
            if (predecessor.next == object) {
                predecessor.next = entry.next;
                break;
            }
            current = predecessor.next;
        }
    }
    // OVL 0xc8bc–0xc90b: direct location and byte slot are independent of
    // the old/new parents' child-head words.
    entry.location = destination;
    entry.slot = new_slot;
    if (destination) {
        const auto former_head = head(destination, new_slot);
        auto &heads = nodes_.at(destination).heads;
        if (new_slot < heads.size())
            heads[new_slot] = object;
        entry.next = former_head;
    }
}

} // namespace gateway
