// Bounded comparison of Gateway's first-room native action matrix with the
// independent StoryModel. This is a research oracle, not a replacement parser.
#include "story_model.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using gateway::StoryAction;
using gateway::StoryModel;
using gateway::StoryMutation;
using gateway::StoryMutationKind;
using gateway::StoryNormalGuard;

constexpr gateway::StoryObjectId book = 0x123;
constexpr gateway::StoryObjectId drawer = 0x124;
constexpr gateway::StoryObjectId desk = 0x125;
constexpr gateway::StoryObjectId player = 0xd3;
constexpr gateway::StoryObjectId room = 0xee;
constexpr std::array slots{18, 21, 25, 28, 31, 34, 37};

std::string trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return std::string(text);
}

std::vector<std::string> split(std::string_view text, char separator) {
    std::vector<std::string> words;
    while (true) {
        const auto end = text.find(separator);
        words.push_back(trim(text.substr(0, end)));
        if (end == text.npos)
            return words;
        text.remove_prefix(end + 1);
    }
}

std::string format_mutations(const std::vector<StoryMutation> &mutations) {
    if (mutations.empty())
        return "—";
    std::ostringstream out;
    for (const auto &mutation : mutations) {
        if (out.tellp() > 0)
            out << ", ";
        switch (mutation.kind) {
        case StoryMutationKind::SetFlag: out << "set"; break;
        case StoryMutationKind::ClearFlag: out << "clear"; break;
        case StoryMutationKind::SetLocation: out << "location-set"; break;
        }
        out << "(0x" << std::hex << std::setfill('0') << std::setw(3) << mutation.object
            << ",0x" << std::setw(2) << mutation.argument << ')';
    }
    return out.str();
}

struct ReplayResult {
    bool dispatched;
    std::vector<StoryMutation> changes;
};

// The gate is deliberately OUTSIDE StoryModel. The report establishes only
// that an action did not reach the dispatcher, not which parser/visibility
// check rejected it. These first-room admission rules are test hypotheses.
class FirstRoomReplay {
public:
    FirstRoomReplay() {
        state_.define(book, drawer);
        state_.define(drawer, desk);
        state_.define(desk, room);
        state_.define(player, room);
        state_.define(room);
        state_.initial_flag(drawer, 0x0e);
        state_.initial_flag(drawer, 0x07);
        state_.initial_flag(player, 0x07);
        state_.initial_flag(room, 0x07);
    }

    ReplayResult run(std::string_view command) {
        if (command == "open drawer" || command == "close drawer") {
            const auto action = command == "open drawer" ? StoryAction::Open
                                                         : StoryAction::Close;
            if (state_.evaluate_normal_open_close(action, drawer) != StoryNormalGuard::Allowed)
                return {true, {}};
            auto changes = state_.apply_success(action, drawer, player, room);
            if (action == StoryAction::Open && state_.location(book) == drawer) {
                // Native OPEN reveals the book and sets its persistent 0x07.
                const std::array revealed{book};
                auto observed = state_.observe(revealed);
                changes.insert(changes.end(), observed.begin(), observed.end());
            }
            return {true, std::move(changes)};
        }
        if (command == "take book") {
            const auto where = state_.location(book);
            const bool visible = where == room ||
                                 (where == drawer && state_.flag(drawer, 0x0c));
            if (!visible)
                return {false, {}};
            return {true, state_.apply_success(StoryAction::Take, book, player, room)};
        }
        if (command == "drop book") {
            if (state_.location(book) != player)
                return {false, {}};
            return {true, state_.apply_success(StoryAction::Drop, book, player, room)};
        }
        if (command == "look" || command == "inventory")
            return {true, {}}; // The six observed scenarios have no new visible objects here.
        throw std::invalid_argument("unknown scripted first-room command: " +
                                    std::string(command));
    }

private:
    StoryModel state_;
};

std::size_t check(std::istream &in) {
    std::string line, scenario;
    FirstRoomReplay model;
    std::size_t step = 0, scenarios = 0;
    bool effect_table = false;
    while (std::getline(in, line)) {
        if (line.starts_with("## ")) {
            if (!scenario.empty() && step != slots.size())
                throw std::runtime_error(scenario + ": expected seven command slots");
            scenario = trim(std::string_view(line).substr(3));
            if (scenario.empty())
                throw std::runtime_error("empty scenario heading");
            model = FirstRoomReplay{};
            step = 0;
            effect_table = false;
            ++scenarios;
            continue;
        }
        if (line.starts_with("### ")) {
            effect_table = false; // Extra diagnostics are not object-state assertions.
            continue;
        }
        if (line.starts_with("| Guest slot | Typed command | Dispatcher return | ")) {
            effect_table = true;
            continue;
        }
        if (scenario.empty() || !effect_table || !line.starts_with("| "))
            continue;
        const auto columns = split(line, '|');
        if (columns.size() != 6 || !columns[1].ends_with('s') ||
            !std::isdigit(static_cast<unsigned char>(columns[1][0])))
            continue; // Markdown table headings/separators.
        if (step == slots.size() || columns[1] != std::to_string(slots[step]) + "s")
            throw std::runtime_error(scenario + ": unexpected guest command slot " + columns[1]);
        const auto result = model.run(columns[2]);
        const auto expected_dispatch = result.dispatched ? "0x1" : "(no dispatch observed)";
        const auto expected_effects = format_mutations(result.changes);
        if (columns[3] != expected_dispatch || columns[4] != expected_effects) {
            throw std::runtime_error(scenario + " at " + columns[1] + " (" + columns[2] +
                                     "): native dispatch=" + columns[3] +
                                     ", model=" + expected_dispatch + "; native changes=" +
                                     columns[4] + ", model=" + expected_effects);
        }
        ++step;
    }
    if (!scenario.empty() && step != slots.size())
        throw std::runtime_error(scenario + ": expected seven command slots");
    if (!scenarios)
        throw std::runtime_error("no action-matrix scenarios found");
    return scenarios;
}
} // namespace

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "Usage: gateway-story-check ACTION-MATRIX-REPORT.md\n";
        return 2;
    }
    std::ifstream in(argv[1]);
    if (!in) {
        std::cerr << "Cannot open action-matrix report: " << argv[1] << '\n';
        return 2;
    }
    try {
        const auto count = check(in);
        std::cout << "First-room C++ model matches " << count
                  << " native action-matrix scenarios (bounded comparison).\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
