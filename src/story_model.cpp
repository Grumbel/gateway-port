#include "story_model.hpp"
#include <stdexcept>

namespace gateway {

void StoryModel::define(StoryObjectId id, std::optional<StoryObjectId> location) {
    if (!id || !objects_.emplace(id, Object{{}, location}).second)
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

} // namespace gateway
