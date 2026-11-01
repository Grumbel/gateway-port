#include "story_model.hpp"
#include <stdexcept>

using gateway::StoryChildCandidate;
using gateway::StoryLinkedChildren;
using gateway::StoryModel;
using gateway::StoryObjectId;

void require(bool good, const char *message) {
    if (!good)
        throw std::runtime_error(message);
}

int main() {
    constexpr StoryObjectId book = 0x123, drawer = 0x124, player = 0xd3, room = 0xee;
    StoryLinkedChildren links;
    links.define(drawer, 3);
    links.define(book, 0, drawer, 0);
    links.define(player, 3);
    links.define(room, 3);
    links.initial_head(drawer, 0, book);

    StoryModel visibility;
    visibility.define(drawer);
    visibility.initial_flag(drawer, 0x13);
    visibility.initial_flag(drawer, 0x0c);
    require(visibility.eligible_child_slots(drawer, 2, links.heads(drawer), true) ==
                std::vector<StoryChildCandidate>{{0, book}},
            "initial drawer child not visible through native slot predicate");

    // The trace reports type=2 and a word write at +0x14:
    // TAKE changes drawer[slot0] from 0x123 to 0, independent of flag 0x0c.
    links.move(book, player, 0);
    require(links.head(drawer, 0) == 0 && links.head(player, 0) == book &&
                links.location(book) == player && links.slot(book) == 0 &&
                links.next(book) == 0 &&
                visibility.eligible_child_slots(drawer, 2, links.heads(drawer), true).empty(),
            "TAKE did not unlink book and prepend to destination");
    require(visibility.can_enumerate_child_slot(drawer, 2, 0),
            "moving book incorrectly changed the OPEN slot eligibility");

    links.move(book, room, 1);
    require(links.head(player, 0) == 0 && links.head(room, 1) == book &&
                links.location(book) == room && links.slot(book) == 1,
            "DROP did not preserve requested destination slot");
    links.move(book, drawer, 0);
    require(links.head(room, 1) == 0 && links.head(drawer, 0) == book,
            "moving book back did not restore drawer's child word");
    links.move(book, drawer, 0);
    require(links.head(drawer, 0) == book && links.next(book) == 0,
            "repeated relocation duplicated a child in its own slot");

    // Linked-list surgery also has a distinct non-head path. The native
    // routine replaces the predecessor's next word when moving a middle node.
    constexpr StoryObjectId a = 0x201, b = 0x202, c = 0x203;
    StoryLinkedChildren chain;
    chain.define(drawer, 3);
    chain.define(room, 3);
    chain.define(a, 0, drawer, 0, b);
    chain.define(b, 0, drawer, 0, c);
    chain.define(c, 0, drawer, 0);
    chain.initial_head(drawer, 0, a);
    chain.move(b, room, 2);
    require(chain.head(drawer, 0) == a && chain.next(a) == c &&
                chain.head(room, 2) == b && chain.next(b) == 0,
            "middle-child unlink did not splice the next sibling");
    chain.move(c, room, 2);
    require(chain.head(drawer, 0) == a && chain.next(a) == 0 &&
                chain.head(room, 2) == c && chain.next(c) == b,
            "destination prepend did not preserve existing child list");
    chain.move(c, 0, 0);
    require(chain.head(room, 2) == b && chain.location(c) == 0,
            "zero destination did not detach the head child");

    require(chain.head(drawer, 99) == 0, "out-of-range child lookup was not zero");
    bool rejected = false;
    try {
        chain.initial_head(drawer, 99, a);
    } catch (const std::out_of_range &) {
        rejected = true;
    }
    require(rejected, "invalid initial child slot accepted");

    // Snapshot-reconstructed first-room hierarchy (native records reported
    // 2026-11-01). A type-1 room holds a *mixed-type* sibling chain with
    // type-8 picture/plant, type-2 furniture and a type-7 player. Types are
    // decoded upstream; this model follows their logical head/next fields.
    constexpr StoryObjectId picture = 0x175, plant = 0x16f;
    constexpr StoryObjectId locker = 0x150, bed = 0x14f, chair = 0x14e;
    constexpr StoryObjectId commset = 0x131, pvslot = 0x170, desk = 0x125;
    constexpr StoryObjectId debit = 0x126, boots = 0xd7, white = 0xd5, blue = 0xd4;
    StoryLinkedChildren quarters;
    quarters.define(room, 1);
    quarters.define(picture, 0, room, 0, plant);
    quarters.define(plant, 0, room, 0, locker);
    quarters.define(locker, 3, room, 0, bed);
    quarters.define(bed, 3, room, 0, chair);
    quarters.define(chair, 3, room, 0, commset);
    quarters.define(commset, 3, room, 0, desk);
    quarters.define(pvslot, 0, commset, 2);
    quarters.define(desk, 3, room, 0, player);
    quarters.define(debit, 3, desk, 1);
    quarters.define(drawer, 3, desk, 2);
    quarters.define(player, 3, room, 0);
    quarters.define(book, 3, player, 0);
    quarters.define(boots, 3, player, 1, white);
    quarters.define(white, 3, player, 1, blue);
    quarters.define(blue, 3, player, 1);
    quarters.initial_head(room, 0, picture);
    quarters.initial_head(commset, 2, pvslot);
    quarters.initial_head(desk, 1, debit);
    quarters.initial_head(desk, 2, drawer);
    quarters.initial_head(player, 0, book);
    quarters.initial_head(player, 1, boots);
    const std::vector<StoryObjectId> room_children{
        picture, plant, locker, bed, chair, commset, desk, player};
    require(quarters.children(room, 0) == room_children,
            "type-1 room did not recover full mixed-type child chain");
    require(quarters.children(player, 1) ==
                std::vector<StoryObjectId>{boots, white, blue},
            "type-7 player did not recover clothing sibling chain");
    require(quarters.children(desk, 2) == std::vector<StoryObjectId>{drawer} &&
                quarters.children(commset, 2) == std::vector<StoryObjectId>{pvslot},
            "nested native child slots did not survive hierarchy reconstruction");

    // DROP makes book the room's head, with picture as its next sibling.
    // Repeated TAKE must restore 0x175, just as the live 34,354-ms call did.
    quarters.move(book, room, 0);
    require(quarters.head(room, 0) == book && quarters.next(book) == picture &&
                quarters.head(player, 0) == 0,
            "DROP failed to prepend book ahead of room picture");
    rejected = false;
    try {
        quarters.move(book, player, 9);
    } catch (const std::out_of_range &) {
        rejected = true;
    }
    require(rejected && quarters.head(room, 0) == book &&
                quarters.location(book) == room,
            "invalid destination slot partially mutated the native child chain");
    quarters.move(book, player, 0);
    require(quarters.children(room, 0) == room_children &&
                quarters.children(player, 0) == std::vector<StoryObjectId>{book},
            "repeated TAKE did not restore picture as room's first sibling");

    // A *hypothetical* non-head move exercises the unobserved 0xc883 path:
    // PV commset is between chair and desk, but its own PV slot stays nested.
    quarters.move(commset, player, 2);
    require(quarters.children(room, 0) == std::vector<StoryObjectId>{
                picture, plant, locker, bed, chair, desk, player} &&
                quarters.children(player, 2) == std::vector<StoryObjectId>{commset} &&
                quarters.children(commset, 2) == std::vector<StoryObjectId>{pvslot},
            "middle-sibling move corrupted room or nested commset links");

    StoryLinkedChildren inconsistent;
    inconsistent.define(drawer, 3);
    inconsistent.define(room, 3);
    inconsistent.define(a, 0, drawer, 1);  // claims slot 1 but linked in slot 0
    inconsistent.define(b, 0, drawer, 0);  // claims parent without any link
    inconsistent.initial_head(drawer, 0, a);
    rejected = false;
    try {
        (void)inconsistent.children(drawer, 0);
    } catch (const std::logic_error &) {
        rejected = true;
    }
    require(rejected, "chain traversal accepted inconsistent slot metadata");
    rejected = false;
    try {
        inconsistent.move(b, room, 0);
    } catch (const std::logic_error &) {
        rejected = true;
    }
    require(rejected && inconsistent.location(b) == drawer &&
                inconsistent.head(room, 0) == 0,
            "absent member moved despite invalid source parent chain");

    // The DOS loop may hang on corrupt sibling links. The host research model
    // reports malformed cycles rather than silently treating them as valid.
    StoryLinkedChildren malformed;
    malformed.define(drawer, 3);
    malformed.define(a, 0, drawer, 0, b);
    malformed.define(b, 0, drawer, 0, a);
    malformed.define(c, 0, drawer);
    malformed.define(room, 3);
    malformed.initial_head(drawer, 0, a);
    rejected = false;
    try {
        malformed.move(c, room, 0);
    } catch (const std::logic_error &) {
        rejected = true;
    }
    require(rejected && malformed.location(c) == drawer,
            "cyclic sibling list did not fail safely before moving the object");
}
