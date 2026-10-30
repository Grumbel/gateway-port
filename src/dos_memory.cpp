#include "dos_memory.hpp"
#include <algorithm>
#include <stdexcept>

namespace gateway {
DosMemory::DosMemory(std::uint16_t psp, std::uint16_t end) : owner_(psp) {
    if (!psp || psp >= end || end > 0xa000)
        throw std::runtime_error("invalid DOS conventional-memory arena");
    blocks_.push_back({psp, static_cast<std::uint16_t>(end - psp), psp});
}
void DosMemory::coalesce() {
    for (std::size_t i = 0; i + 1 < blocks_.size();) {
        if (!blocks_[i].owner && !blocks_[i + 1].owner) {
            blocks_[i].paragraphs += blocks_[i + 1].paragraphs + 1;
            blocks_.erase(blocks_.begin() + i + 1);
        } else
            ++i;
    }
}
void DosMemory::split(std::size_t i, std::uint16_t paragraphs) {
    const auto old = blocks_[i];
    if (paragraphs == old.paragraphs)
        return;
    blocks_[i].paragraphs = paragraphs;
    blocks_.insert(blocks_.begin() + i + 1,
                   {static_cast<std::uint16_t>(old.segment + paragraphs + 1),
                    static_cast<std::uint16_t>(old.paragraphs - paragraphs - 1), 0});
}
MemoryResult DosMemory::allocate(std::uint16_t paragraphs) {
    coalesce();
    std::uint16_t largest = 0;
    for (std::size_t i = 0; i < blocks_.size(); ++i) {
        if (blocks_[i].owner)
            continue;
        largest = std::max(largest, blocks_[i].paragraphs);
        if (blocks_[i].paragraphs >= paragraphs) {
            split(i, paragraphs);
            blocks_[i].owner = owner_;
            return {0, blocks_[i].segment, paragraphs};
        }
    }
    return {8, 0, largest};
}
MemoryResult DosMemory::release(std::uint16_t segment) {
    for (auto &block : blocks_) {
        if (block.segment == segment && block.owner) {
            block.owner = 0;
            return {};
        }
    }
    return {9};
}
MemoryResult DosMemory::resize(std::uint16_t segment, std::uint16_t paragraphs) {
    coalesce();
    for (std::size_t i = 0; i < blocks_.size(); ++i) {
        if (blocks_[i].segment != segment || !blocks_[i].owner)
            continue;
        if (paragraphs > blocks_[i].paragraphs && i + 1 < blocks_.size() && !blocks_[i + 1].owner) {
            blocks_[i].paragraphs += blocks_[i + 1].paragraphs + 1;
            blocks_.erase(blocks_.begin() + i + 1);
        }
        const auto maximum = blocks_[i].paragraphs;
        // DOS 5 grows as far as possible even when the requested size cannot fit.
        if (paragraphs > maximum)
            return {8, segment, maximum};
        split(i, paragraphs);
        coalesce();
        return {0, segment, paragraphs};
    }
    return {9};
}
void DosMemory::write_mcbs(std::span<std::uint8_t> memory) const {
    for (std::size_t i = 0; i < blocks_.size(); ++i) {
        const auto &block = blocks_[i];
        const std::size_t address = std::size_t(block.segment - 1) * 16;
        if (address + 16 > memory.size())
            throw std::runtime_error("MCB outside guest memory");
        auto mcb = memory.subspan(address, 16);
        std::fill(mcb.begin(), mcb.end(), 0);
        mcb[0] = i + 1 == blocks_.size() ? 'Z' : 'M';
        mcb[1] = block.owner & 255;
        mcb[2] = block.owner >> 8;
        mcb[3] = block.paragraphs & 255;
        mcb[4] = block.paragraphs >> 8;
    }
}
} // namespace gateway
