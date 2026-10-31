#include "dos_fcb.hpp"
#include <iostream>
#include <stdexcept>
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
int main() {
    try {
        auto fcb = gateway::parse_fcb("  C:hello.txt next", 1, {});
        require(fcb.status == 0 && fcb.bytes[0] == 3 &&
                    std::string_view(reinterpret_cast<const char *>(fcb.bytes.data() + 1), 11) ==
                        "HELLO   TXT",
                "drive or uppercase/padding incorrect");
        require(fcb.consumed == 13, "parser did not leave the separator for the next call");
        auto next = gateway::parse_fcb(" 0 258 9\r", 1, {});
        require(next.consumed == 2 && next.bytes[1] == '0', "numeric handoff field incorrect");
        next = gateway::parse_fcb(" 258 9\r", 1, {});
        require(next.consumed == 4 && next.bytes[3] == '8',
                "second numeric handoff field incorrect");
        auto wild = gateway::parse_fcb("*.d?", 0, {});
        require(wild.status == 1 && wild.bytes[8] == '?' && wild.bytes[9] == 'D' &&
                    wild.bytes[10] == '?',
                "wildcard expansion incorrect");
        auto preserved = gateway::parse_fcb("\r", 0x0e, fcb.bytes);
        require(preserved.bytes == fcb.bytes, "missing fields were not preserved");
        require(gateway::parse_fcb("Z:file", 0, {}).status == 255, "unmounted drive accepted");
        auto empty = gateway::parse_fcb("\r", 0, fcb.bytes);
        require(empty.bytes[0] == 0 && empty.bytes[1] == ' ' && empty.bytes[9] == ' ',
                "default fields not cleared");
        require(gateway::parse_fcb("longfilename.longext", 0, {}).consumed == 20,
                "truncated fields did not consume the full name");
        std::cout << "FCB filename checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
