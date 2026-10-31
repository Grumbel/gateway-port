#include "dos_fcb.hpp"
#include <algorithm>
namespace gateway {
ParsedFcb parse_fcb(std::string_view input, std::uint8_t flags,
                    std::array<std::uint8_t, 16> previous) {
    ParsedFcb result{previous};
    auto &at = result.consumed;
    auto peek = [&] { return at < input.size() ? input[at] : '\0'; };
    auto upper = [](unsigned char c) { return c >= 'a' && c <= 'z' ? c - 32 : c; };
    auto valid = [](unsigned char c) {
        return c > 32 && c < 127 &&
               std::string_view(":.;,=+\\/\"[]<>|").find(c) == std::string_view::npos;
    };
    while (peek() == ' ' || peek() == '\t')
        ++at;
    if ((flags & 1) && peek() && std::string_view(":;,=+").find(peek()) != std::string_view::npos)
        ++at;
    while (peek() == ' ' || peek() == '\t')
        ++at;
    if (!(flags & 2))
        result.bytes[0] = 0;
    if (at + 1 < input.size() && input[at + 1] == ':') {
        const auto drive = upper(input[at]);
        if (drive < 'A' || drive > 'Z')
            result.status = 255;
        else {
            result.bytes[0] = drive - 'A' + 1;
            if (drive != 'C')
                result.status = 255; // This runtime mounts only C:.
        }
        at += 2;
    }
    auto field = [&](unsigned offset, unsigned length, bool present, bool preserve) {
        if (!present) {
            if (!preserve)
                std::fill_n(result.bytes.begin() + offset, length, ' ');
            return;
        }
        std::fill_n(result.bytes.begin() + offset, length, ' ');
        unsigned column = 0;
        bool fill_wild = false;
        while (valid(peek())) {
            auto c = upper(static_cast<unsigned char>(peek()));
            ++at;
            if (c == '*')
                fill_wild = true;
            if (fill_wild)
                c = '?';
            if (column < length) {
                result.bytes[offset + column++] = c;
                if (c == '?' && result.status != 255)
                    result.status = 1;
            }
        }
        if (fill_wild)
            std::fill(result.bytes.begin() + offset + column,
                      result.bytes.begin() + offset + length, '?');
    };
    field(1, 8, valid(peek()), flags & 4);
    const bool extension = peek() == '.';
    if (extension)
        ++at;
    field(9, 3, extension, flags & 8);
    std::fill(result.bytes.begin() + 12, result.bytes.end(), 0);
    return result;
}
} // namespace gateway
