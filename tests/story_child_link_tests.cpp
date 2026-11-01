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
