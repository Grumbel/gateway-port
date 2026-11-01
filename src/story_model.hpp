#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <vector>

namespace gateway {

using StoryObjectId = std::uint16_t;

// Research-only state effects, not a replacement for the game's parser,
// permission checks, output text, event handlers, or containment lists.
enum class StoryAction { Look, Open, Close, Take, Drop, Inventory };
enum class StoryMutationKind { SetFlag, ClearFlag, SetLocation };

// Result of the *inner, common* OPEN/CLOSE guard sequence in Gateway's
// section-5 handler. The complete DOS action has earlier mode, reachability,
// special-object and event-handler decisions which are NOT represented here.
enum class StoryNormalGuard {
    Allowed,
    AlreadyOpen,
    AlreadyClosed,
    Blocked,
    CannotOperate,
};

struct StoryMutation {
    StoryMutationKind kind;
    StoryObjectId object;
    std::uint16_t argument;

    bool operator==(const StoryMutation &) const = default;
};

class StoryModel {
public:
    // Initial state is supplied by the caller. This sparse logical model
    // currently does not interpret the DOS object-type/property masks.
    void define(StoryObjectId object, std::optional<StoryObjectId> location = {});
    void initial_flag(StoryObjectId object, std::uint16_t property, bool set = true);
    bool flag(StoryObjectId object, std::uint16_t property) const;
    std::optional<StoryObjectId> location(StoryObjectId object) const;

    // Mirrors the verified predicate order at GATEMAIN.OVL 0xea65–0xeb1e
    // (OPEN) and 0xd706–0xd7d4 (CLOSE) on the ordinary inner path only.
    // Does not change state; Allowed is not proof the full DOS command succeeds.
    // Rejects actions other than Open and Close.
    StoryNormalGuard evaluate_normal_open_close(StoryAction action,
                                                StoryObjectId target) const;

    // Only the mutations of an already-successful action, as observed for
    // Gateway's first-room drawer/book scenario. The original DOS handlers
    // still decide reachability, action guards, and error/description text.
    std::vector<StoryMutation> apply_success(StoryAction action, StoryObjectId target,
                                             StoryObjectId player, StoryObjectId room);

    // Visual disclosure is separate from the OPEN handler. Property 0x07 is
    // *probably* seen/known; its full meaning is not established yet.
    std::vector<StoryMutation> observe(std::span<const StoryObjectId> objects);

private:
    struct Object {
        std::set<std::uint16_t> flags;
        std::optional<StoryObjectId> location;
    };
    std::map<StoryObjectId, Object> objects_;
    void set_flag(std::vector<StoryMutation> &changes, StoryObjectId object,
                  std::uint16_t property, bool value);
    void move(std::vector<StoryMutation> &changes, StoryObjectId object,
              StoryObjectId destination);
};

} // namespace gateway
