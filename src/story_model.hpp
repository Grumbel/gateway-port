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
// permission checks, output text, or event handlers.
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

// TAKE's direct-location test and DROP's recursive object-relation test in
// the original selector callbacks. These are not parser visibility tests or
// complete action-permission decisions.
enum class StoryCarryPrecheck { Continue, AlreadyHolding, NotHolding };

struct StoryMutation {
    StoryMutationKind kind;
    StoryObjectId object;
    std::uint16_t argument;

    bool operator==(const StoryMutation &) const = default;
};

// A single slot that survives the resident parser's *optional* eligibility
// check and its nonzero-child test. This is not a resolved parser target.
struct StoryChildCandidate {
    std::uint16_t slot;
    StoryObjectId child;

    bool operator==(const StoryChildCandidate &) const = default;
};

class StoryModel {
public:
    // Initial state is supplied by the caller. This sparse logical model
    // currently does not interpret the DOS object-type/property masks.
    void define(StoryObjectId object, std::optional<StoryObjectId> location = {});
    void initial_flag(StoryObjectId object, std::uint16_t property, bool set = true);
    bool flag(StoryObjectId object, std::uint16_t property) const;
    std::optional<StoryObjectId> location(StoryObjectId object) const;

    // Type-6 objects in Gateway have a sequence of relationship slots. The
    // first one is the direct location, and these are the remaining slots.
    // Other types' native accessor repeats their single location. Callers
    // supply only verified links; this sparse model does not read DOS types.
    void set_relation_tail(StoryObjectId object, std::span<const StoryObjectId> links);

    // Bounded, cycle-safe counterpart to GATEMAIN.EXE 0x2603 / 0157:00c5.
    // Tests paths of length >=1 from source to wanted, following ordered
    // relation slots and stopping a list on its first zero word. Unlike the
    // original recursive assembly this will not hang on cyclic object data.
    bool follows_relation_path(StoryObjectId source, StoryObjectId wanted) const;

    // EXE 0385:0a9a (file 0x53c4) gates which *child slots* the resident
    // parser's candidate iterator visits when its restriction flag is set.
    // Type is the caller's native object type (1–8), slot is zero-based.
    // This is NOT a complete parser visibility or action-reachability check.
    bool can_enumerate_child_slot(StoryObjectId object, std::uint8_t type,
                                  std::uint16_t slot) const;

    // One local stage of EXE 1000:0000: apply the optional slot gate and
    // omit zero child IDs. The caller supplies the ordered child accessor
    // results from a particular snapshot. No recursion, name resolution,
    // command acceptance or implied child-pointer mutations are performed.
    std::vector<StoryChildCandidate> eligible_child_slots(
        StoryObjectId object, std::uint8_t type, std::span<const StoryObjectId> children,
        bool restricted) const;

    // Mirrors the verified predicate order at GATEMAIN.OVL 0xea65–0xeb1e
    // (OPEN) and 0xd706–0xd7d4 (CLOSE) on the ordinary inner path only.
    // Does not change state; Allowed is not proof the full DOS command succeeds.
    // Rejects actions other than Open and Close.
    StoryNormalGuard evaluate_normal_open_close(StoryAction action,
                                                StoryObjectId target) const;

    // Translation of the relevant branch of Gateway section-5 callbacks at
    // GATEMAIN.OVL 0xfbaf (TAKE) and 0xd804 (DROP). Neither checks whether
    // the parser can currently resolve or reach the object.
    StoryCarryPrecheck evaluate_carry_precheck(StoryAction action, StoryObjectId target,
                                              StoryObjectId player) const;

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
        std::vector<StoryObjectId> relation_tail;
    };
    std::map<StoryObjectId, Object> objects_;
    void set_flag(std::vector<StoryMutation> &changes, StoryObjectId object,
                  std::uint16_t property, bool value);
    void move(std::vector<StoryMutation> &changes, StoryObjectId object,
              StoryObjectId destination);
};

} // namespace gateway
