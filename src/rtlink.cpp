#include "rtlink.hpp"
#include <algorithm>
#include <stdexcept>

namespace gateway {
namespace {
std::uint16_t word(std::span<const std::uint8_t> data, std::size_t at) {
    if (at > data.size() || data.size() - at < 2)
        throw std::runtime_error("truncated RTLink word");
    return data[at] | (std::uint16_t(data[at + 1]) << 8);
}
void validate_section(const RtlinkSection &section) {
    if (!section.memory_paragraphs || section.initialized_paragraphs > section.memory_paragraphs ||
        section.image.size() > section.initialized_size())
        throw std::runtime_error("invalid RTLink section size");
    for (const auto &fixup : section.fixups)
        if (fixup.image_offset > section.image.size() ||
            section.image.size() - fixup.image_offset < 2)
            throw std::runtime_error("RTLink fixup outside file-backed section bytes");
}
} // namespace
RtlinkDirectory parse_rtlink(std::span<const std::uint8_t> exe,
                             std::span<const std::uint8_t> overlay,
                             std::size_t manager_file_offset) {
    if (manager_file_offset > exe.size())
        throw std::runtime_error("RTLink manager outside executable");
    const auto manager = exe.subspan(manager_file_offset);
    const auto count = word(manager, 0xc19);
    constexpr std::size_t table = 0xc1f, stride = 18;
    if (count < 2 || std::size_t(count) * stride > 65536 - table ||
        table + std::size_t(count) * stride > manager.size())
        throw std::runtime_error("invalid RTLink section directory bounds");
    const auto sentinel = manager.subspan(table + (count - 1) * stride, stride);
    if (std::any_of(sentinel.begin(), sentinel.end(), [](auto byte) { return byte != 0; }))
        throw std::runtime_error("missing empty RTLink sentinel");
    RtlinkDirectory directory{manager_file_offset, {}};
    for (unsigned id = 0; id + 1 < count; ++id) {
        const auto at = table + id * stride;
        const auto record = manager.subspan(at, stride);
        if (record[7] != 4)
            throw std::runtime_error("unsupported RTLink section flags");
        const auto filename_pointer = word(record, 2);
        const auto filename_start = std::size_t(filename_pointer) + 1;
        if (filename_start >= std::min(manager.size(), std::size_t(65536)))
            throw std::runtime_error("RTLink filename outside manager segment");
        std::string filename;
        bool terminated = false;
        for (auto p = filename_start;
             p < std::min(manager.size(), std::size_t(65536)) && p < filename_start + 128; ++p) {
            const auto byte = manager[p];
            if (!byte) {
                terminated = true;
                break;
            }
            if (byte < 32 || byte > 126)
                throw std::runtime_error("invalid RTLink filename byte");
            filename += char(byte);
        }
        if (!terminated || filename.empty())
            throw std::runtime_error("invalid RTLink filename");
        const auto relocation_count = word(record, 10);
        const auto relocation_offset =
            (std::size_t(word(record, 4)) | (std::size_t(record[6]) << 16)) * 16;
        const auto code_offset = relocation_offset + ((std::size_t(relocation_count) + 3) / 4) * 16;
        const auto code_size = std::size_t(word(record, 16)) * 16;
        if (relocation_offset > overlay.size() ||
            relocation_count * 4u > overlay.size() - relocation_offset ||
            code_offset >= overlay.size() || code_size == 0)
            throw std::runtime_error("RTLink section outside overlay file");
        const auto available = std::min(code_size, overlay.size() - code_offset);
        // The final file ends mid-paragraph. Anything more than alignment padding is truncation.
        if (code_size - available > 15)
            throw std::runtime_error("truncated RTLink section image");
        RtlinkSection section{
            id,
            word(record, 0),
            word(record, 8),
            word(record, 16),
            word(record, 12),
            word(record, 14),
            std::move(filename),
            relocation_offset,
            code_offset,
            {},
            {overlay.begin() + code_offset, overlay.begin() + code_offset + available}};
        for (unsigned i = 0; i < relocation_count; ++i) {
            const auto offset = word(overlay, relocation_offset + i * 4);
            const auto segment = word(overlay, relocation_offset + i * 4 + 2);
            const auto absolute = std::size_t(segment) * 16 + offset;
            const auto base = std::size_t(section.linked_segment) * 16;
            if (absolute < base)
                throw std::runtime_error("RTLink fixup precedes section");
            section.fixups.push_back({offset, segment, absolute - base});
        }
        validate_section(section);
        if (section.parent != 0xffff && section.parent >= id)
            throw std::runtime_error("unsupported forward/cyclic RTLink parent");
        directory.sections.push_back(std::move(section));
    }
    for (std::size_t i = 0; i < directory.sections.size(); ++i) {
        const auto &section = directory.sections[i];
        if (section.parent != 0xffff) {
            const auto &parent = directory.sections[section.parent];
            if (std::size_t(parent.linked_segment) + parent.memory_paragraphs !=
                section.linked_segment)
                throw std::runtime_error("RTLink child is not adjacent to its parent");
        }
        const auto end = section.image_file_offset + section.image.size();
        if ((i == 0 && section.relocation_file_offset != 0) ||
            (i + 1 < directory.sections.size() &&
             end != directory.sections[i + 1].relocation_file_offset) ||
            (i + 1 == directory.sections.size() && end != overlay.size()))
            throw std::runtime_error("RTLink sections do not cover the overlay file contiguously");
        if (section.filename != directory.sections.front().filename)
            throw std::runtime_error("multiple RTLink overlay files are unsupported");
    }
    return directory;
}
std::vector<std::uint8_t> relocate_rtlink(const RtlinkSection &section, std::uint16_t factor) {
    validate_section(section);
    auto image = section.image;
    for (const auto &fixup : section.fixups) {
        const auto value = std::uint16_t(word(image, fixup.image_offset) + factor);
        image[fixup.image_offset] = value & 255;
        image[fixup.image_offset + 1] = value >> 8;
    }
    return image;
}
void apply_rtlink(std::span<std::uint8_t> memory, const RtlinkSection &section,
                  std::uint16_t factor) {
    const auto relocated =
        relocate_rtlink(section, factor); // Validate before changing guest memory.
    const auto segment = std::size_t(section.linked_segment) + factor;
    const auto base = segment * 16;
    if (segment > 0xffff || base > std::min(memory.size(), std::size_t(0xa0000)) ||
        section.memory_size() > std::min(memory.size(), std::size_t(0xa0000)) - base)
        throw std::out_of_range("RTLink section outside conventional memory");
    std::copy(relocated.begin(), relocated.end(), memory.begin() + base);
    std::fill(memory.begin() + base + section.initialized_size(),
              memory.begin() + base + section.memory_size(), 0);
}
} // namespace gateway
