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

// A room-exit callback's AX is not an action-handled flag. In particular,
// zero may accompany real state mutations that do *not* leave the room.
struct StoryCallbackOutcome {
    StoryObjectId return_ax = 0;
    std::vector<StoryMutation> changes;

    bool operator==(const StoryCallbackOutcome &) const = default;
};

// Section-51 Study room event-1's inner therapy-action matcher. These are
// byte-sized *native parser action codes*, not StoryAction enum values or
// player-facing command strings. A zero target is a native wildcard.
struct StoryStudyActionRule {
    std::uint8_t action_code;
    StoryObjectId target_object;
    std::uint8_t alternative_code_1 = 0;
    std::uint8_t alternative_code_2 = 0;
    bool has_alternatives = false;

    bool operator==(const StoryStudyActionRule &) const = default;
};

enum class StoryStudyMatch { Ineligible, WrongObject, WrongAction, Matched };

// Guest fields [DS:9660]:7904, [DS:967a]:7906, [DS:967c]:7908,
// [DS:9662]:7909, expressed as a small value object. This has no authority
// over parser state, object locations or the real DOS narrative output.
struct StoryStudyResponseState {
    std::int16_t progress = 0;
    std::uint16_t previous_prompt_stage = 0;
    bool reminder_shown = false;
    bool action_matched = false;

    bool operator==(const StoryStudyResponseState &) const = default;
};

enum class StoryStudyResponsePath {
    NewPrompt, RepeatPrompt, Insistence, Accepted, Completion,
};

struct StoryStudyResponseOutcome {
    StoryStudyResponseState next;
    StoryStudyResponsePath path;
    std::uint16_t story_token = 0;  // Original F000 offset only, not a C string.
    std::uint16_t table_stage = 0;  // 0 means no far-pointer table lookup.
    bool calls_auxiliary_handler = false;

    bool operator==(const StoryStudyResponseOutcome &) const = default;
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

    // Only callback 0x2b's Tanning Room (0x29a) UP/event-0x0f branch:
    // if the player's *direct* relation is couch 0x29c, get up into room
    // 0x29a, but return AX=0 either way. Verified against two native replays.
    // This does not translate the callback's other VR-room paths, text output,
    // ordinary navigation, or action/event dispatch.
    StoryCallbackOutcome apply_tanning_up(StoryObjectId player);

    // Callback 0x2b's VR Terminal (0x29b) UP path. The caller supplies the
    // native 0157:005e(collar 0x2a8, player, slot 1) predicate; this sparse
    // model does not infer equipment slots from ordinary object locations.
    // An equipped collar blocks getting up. Otherwise, nested players move
    // into the terminal; both paths return AX=0.
    StoryCallbackOutcome apply_vr_terminal_up(StoryObjectId player, bool collar_equipped);

    // Callback 0x2b's Beach (0x2ae) UP path: get up from a nested location
    // and clear property 0x0a on glass 0x2b4. A player already directly in
    // Beach stays put. All paths return AX=0, not a navigable destination.
    StoryCallbackOutcome apply_beach_up(StoryObjectId player);

    // Callback 0x2b's Study (0x2bc) UP path. Original 0157:0546 first tests
    // the player's immediate parent against leather couch 0x2be, then signed
    // word ES:7904 must lie in [3,19]. The caller supplies that native word;
    // its dialogue/state progression is not reconstructed here. In-range UP
    // gets off the couch, returning AX=0 rather than a room destination.
    StoryCallbackOutcome apply_study_up(StoryObjectId player, std::int16_t progress);

    // Callback 0x05, shared by Study DOWN and Guess Your Weight booth DOWN.
    // For the type-7 player, native 0157:000c checks its single direct
    // relation slot: maple desk 0x2bf or scale 0x087. On the desk, signed
    // progress 9..12 forbids getting down; the scale has no progress gate.
    // The caller supplies the current enclosing room and native Study word.
    // AX remains zero even when 1c24:13d5 moves the player into that room.
    StoryCallbackOutcome apply_desk_scale_down(StoryObjectId player,
                                               StoryObjectId current_room,
                                               std::int16_t study_progress);

    // GATEMAIN.OVL 0x6b6c0–0x6b7e8 (Study room event 1) compares the
    // action code at [DS:9654]:e664 and parsed object at [DS:9658]:055e
    // against per-stage records [DS:9652]:02a8 + 4*progress and five
    // alternate-code records at :02fc. Only player actor 0x0d3, stages
    // 1..20 (20 = terminal SLEEP), and an unset :7909 completion byte enter
    // this bounded path.
    // The native code handles other event-1 branches and writes :7909;
    // this read-only predicate neither implements those nor advances :7904.
    static std::optional<StoryStudyActionRule> study_action_rule(std::int16_t progress);
    static StoryStudyMatch evaluate_study_action(std::int16_t progress,
                                                 std::uint8_t native_action_code,
                                                 StoryObjectId parsed_object,
                                                 StoryObjectId actor,
                                                 bool action_already_matched);

    // Original section-51 function OVL:0x6ce85–0x6d04d. The *caller*
    // supplies native 0157:000c direct-membership results for couch 0x2be
    // and desk 0x2bf; they are not inferred from the model's locations.
    // This models only the bounded prompt/retry/acceptance control path and
    // four guest fields, not the actual text service or auxiliary call.
    // Nullopt marks unsupported/corrupt stages which could index beyond the
    // original action/prompt table. Stage 20's *unmatched* branch still has
    // a valid terminal prompt pointer; a matched stage 20 calls completion.
    static std::optional<StoryStudyResponseOutcome> evaluate_study_response(
        StoryStudyResponseState state, bool player_on_couch, bool player_on_desk);

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

// Research-only, type-agnostic representation of *decoded* child-head and
// sibling links. The observed room is type 1, its inventory-holder is type 7,
// and its head/sibling chain includes type 8 and type 2 objects. Native field
// offsets are decoded separately by inspect_object_state.py; this model does
// not claim to implement every DOS type accessor, permission check or parser.
class StoryLinkedChildren {
public:
    void define(StoryObjectId object, std::uint16_t child_slots,
                StoryObjectId location = 0, std::uint8_t slot = 0,
                StoryObjectId next = 0);
    void initial_head(StoryObjectId parent, std::uint16_t slot, StoryObjectId child);
    StoryObjectId head(StoryObjectId parent, std::uint16_t slot) const;
    StoryObjectId next(StoryObjectId object) const;
    StoryObjectId location(StoryObjectId object) const;
    std::uint8_t slot(StoryObjectId object) const;
    std::vector<StoryObjectId> heads(StoryObjectId parent) const;
    // Expand one complete sibling chain and check the children's location/
    // slot metadata. Reject dangling IDs and malformed cycles.
    std::vector<StoryObjectId> children(StoryObjectId parent, std::uint16_t slot) const;

    // Emulate the logical, observed head/sibling path of OVL file 0xc830:
    // unlink from old parent's chain, assign location and slot, then insert
    // at the front of the new parent's chain. Zero destination detaches.
    // The original lacks these safety guards; ours rejects invalid chains.
    void move(StoryObjectId object, StoryObjectId destination, std::uint8_t slot);

private:
    struct Node {
        StoryObjectId location = 0, next = 0;
        std::uint8_t slot = 0;
        std::vector<StoryObjectId> heads;
    };
    std::map<StoryObjectId, Node> nodes_;
};

} // namespace gateway
