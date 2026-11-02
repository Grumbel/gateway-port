#include "story_model.hpp"
#include <algorithm>
#include <stdexcept>

namespace gateway {

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
