#include "object_flags.hpp"

namespace gateway {
bool object_flag(const GuestByteReader &read, std::uint16_t ds, std::uint16_t object,
                 std::uint16_t property) {
    const auto word = [&](std::uint16_t segment, std::uint16_t offset) {
        return std::uint16_t(read(segment, offset) |
                             (std::uint16_t(read(segment, std::uint16_t(offset + 1))) << 8));
    };
    if (!object || object >= 0x8000 || !property)
        return false;
    if (object > word(word(ds, 0x8d54), 0x38dc))
        return false;
    const auto directory = word(ds, 0x8d50);
    const auto record = std::uint16_t(object * 6 + 0xba);
    const auto type = read(directory, record);
    if (type < 1 || type > 8)
        return false;
    const auto properties = word(ds, 0x8d56);
    const auto index = std::uint16_t(property * 4);
    if (!(word(properties, std::uint16_t(index + 0x16)) & (1u << (type - 1))))
        return false;
    const auto bit = read(properties, std::uint16_t(index + 0x14));
    // The original adds to the offset without carrying into the segment.
    const auto offset = std::uint16_t(word(directory, std::uint16_t(record + 2)) + 4 + bit / 8);
    const auto segment = word(directory, std::uint16_t(record + 4));
    return (read(segment, offset) >> (bit & 7)) & 1;
}
} // namespace gateway
