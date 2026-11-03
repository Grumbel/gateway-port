// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "dos_files.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
namespace fs = std::filesystem;
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void store(const fs::path &path, std::string_view bytes) {
    std::ofstream(path, std::ios::binary) << bytes;
}
std::string contents(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
std::span<const std::uint8_t> bytes(std::string_view text) {
    return {reinterpret_cast<const std::uint8_t *>(text.data()), text.size()};
}
template <class F> void error(unsigned expected, F operation) {
    try {
        operation();
    } catch (const gateway::DosIoError &e) {
        require(e.code == expected, "unexpected DOS error");
        return;
    }
    throw std::runtime_error("operation unexpectedly succeeded");
}
struct Fixture {
    fs::path root;
    Fixture() {
        auto pattern = (fs::temp_directory_path() / "gateway-files-XXXXXX").string();
        const auto name = ::mkdtemp(pattern.data());
        if (!name)
            throw std::runtime_error("cannot create fixture");
        root = name;
        fs::create_directory(root / "assets");
    }
    ~Fixture() {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
};
int main() {
    try {
        Fixture fixture;
        const auto assets = fixture.root / "assets", state = fixture.root / "state";
        store(assets / "Data.DAT", "ABCDE");
        gateway::DosDrive drive(assets, state), readonly(assets);
        require(drive.read_all("c:\\data.dat").size() == 5, "case-insensitive asset read failed");
        require(!fs::exists(state), "reading assets created state directory");
        error(2, [&] { readonly.open("RESTART.DAT", 1); });
        error(5, [&] { readonly.open("Data.DAT", 2); });
        error(5, [&] { readonly.create("RESTART.DAT", 0); });
        require(drive.attributes("DATA.DAT") == 0x21, "original attributes not read-only");
        {
            auto file = drive.open("DATA.DAT", 2);
            file.seek(1, 0);
            require(file.write(bytes("xy")) == 2, "short state write");
            file.seek(0, 0);
            std::array<std::uint8_t, 5> buffer{};
            require(file.read(buffer) == 5 && std::string(buffer.begin(), buffer.end()) == "AxyDE",
                    "mixed read/write cursor failed");
            file.seek(3, 0);
            file.write({});
            require(file.seek(0, 2) == 3, "zero-byte write did not truncate");
            file.seek(5, 0);
            file.write({});
            require(file.seek(0, 2) == 5, "zero-byte write did not extend");
        }
        require(contents(assets / "Data.DAT") == "ABCDE", "original asset modified");
        require(contents(state / "DATA.DAT") == std::string("Axy\0\0", 5), "state file incorrect");
        gateway::DosDrive reopened(assets, state);
        require(reopened.read_all("data.dat")[1] == 'x', "state did not persist or shadow asset");
        require(drive.attributes("DATA.DAT") == 0x20, "state attributes not writable");
        {
            auto file = drive.create("restart.dat", 0);
            file.write(bytes("state"));
        }
        require(reopened.read_all("RESTART.DAT").size() == 5, "created state not readable");
        {
            auto file = drive.create("RESTART.DAT", 0);
        }
        require(contents(state / "RESTART.DAT").empty(), "create did not truncate");
        {
            auto file = drive.create("Data.DAT", 0);
            file.write(bytes("new"));
        }
        require(contents(assets / "Data.DAT") == "ABCDE", "shadow create truncated original");
        error(5, [&] {
            auto file = drive.open("RESTART.DAT", 0);
            file.write(bytes("x"));
        });
        error(5, [&] {
            auto file = drive.open("RESTART.DAT", 1);
            std::array<std::uint8_t, 1> b{};
            file.read(b);
        });
        error(5, [&] { drive.create("../escape", 0); });
        error(5, [&] { drive.create("C:\\sub\\escape", 0); });
        error(12, [&] { drive.open("DATA.DAT", 3); });
        error(5, [&] { drive.create("GATE*.SAV", 0); });
        error(5, [&] { drive.open("GATE?.SAV", 1); });
        store(assets / "Gate001.Sav", "original");
        store(assets / "GATE002.SAV", "two");
        store(assets / "PLAIN", "no extension");
        store(state / "GATE001.SAV", "saved");
        store(state / "gate003.sav", "third");
        const auto saves = drive.list("gate*.sav", 0);
        require(saves.size() == 3 && saves[0].name == "GATE001.SAV" && saves[0].size == 5 &&
                    saves[0].attributes == 0x20 && saves[1].attributes == 0x21 &&
                    saves[2].name == "GATE003.SAV",
                "merged wildcard search incorrect");
        require(drive.list("GATE00?.SAV", 0).size() == 3 && drive.list("MISSING*.SAV", 0).empty(),
                "DOS wildcard/no-match behavior incorrect");
        const auto all = drive.list("*.*", 0);
        require(
            std::any_of(all.begin(), all.end(), [](const auto &e) { return e.name == "PLAIN"; }),
            "DOS *.* omitted extensionless file");
        require(contents(assets / "Gate001.Sav") == "original", "search changed original save");
        store(fixture.root / "outside", "untouched");
        fs::create_symlink(fixture.root / "outside", state / "LINK.DAT");
        error(5, [&] { drive.create("LINK.DAT", 0); });
        error(5, [&] { drive.open("LINK.DAT", 0); });
        fs::create_hard_link(assets / "Data.DAT", state / "HARD.DAT");
        error(5, [&] { drive.create("HARD.DAT", 0); });
        error(5, [&] { drive.open("HARD.DAT", 2); });
        require(contents(assets / "Data.DAT") == "ABCDE" &&
                    contents(fixture.root / "outside") == "untouched",
                "linked original/outside file modified");
        bool rejected = false;
        try {
            gateway::DosDrive invalid(assets, assets / "state");
        } catch (const std::runtime_error &) {
            rejected = true;
        }
        require(rejected && !fs::exists(assets / "state"),
                "state directory allowed inside originals");
        std::cout << "DOS file overlay checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
