#include "vga.hpp"
#include <algorithm>
#include <stdexcept>

namespace gateway {
Vga::Vga() { set_mode(3); }
void Vga::set_mode(std::uint8_t mode, bool clear) {
    if (mode != 3 && mode != 0x0d && mode != 0x0e && mode != 0x10 && mode != 0x12 && mode != 0x13)
        throw std::runtime_error("unsupported VGA mode");
    mode_ = mode;
    text_page_ = 0;
    graphics_page_ = 0;
    if (mode == 3 && clear) {
        for (std::size_t i = 0; i < text_.size(); i += 2) {
            text_[i] = ' ';
            text_[i + 1] = 7;
        }
    }
    seq_.fill(0);
    gc_.fill(0);
    crtc_.fill(0);
    attr_.fill(0);
    seq_[2] = 15;
    seq_[4] = mode == 0x13 ? 0x0e : 0x06;
    gc_[7] = 15;
    gc_[8] = 255;
    crtc_[0x13] = width() / (mode == 0x13 ? 8 : 16);
    // Disable split-screen address reset until the guest requests one.
    crtc_[0x18] = 0xff;
    crtc_[0x07] |= 0x10;
    crtc_[0x09] |= 0x40;
    attr_data_ = false;
    for (auto &plane : planes_)
        if (clear)
            plane.fill(0);
    latch_.fill(0);
    for (unsigned i = 0; i < 64; ++i) {
        dac_[i] = {static_cast<std::uint8_t>(((i & 4) ? 42 : 0) + ((i & 32) ? 21 : 0)),
                   static_cast<std::uint8_t>(((i & 2) ? 42 : 0) + ((i & 16) ? 21 : 0)),
                   static_cast<std::uint8_t>(((i & 1) ? 42 : 0) + ((i & 8) ? 21 : 0))};
    }
    constexpr std::array<std::uint8_t, 16> ega{0,  1,  2,  3,  4,  5,  20, 7,
                                               56, 57, 58, 59, 60, 61, 62, 63};
    std::copy(ega.begin(), ega.end(), attr_.begin());
}
int Vga::width() const { return mode_ == 0x0d || mode_ == 0x13 ? 320 : 640; }
int Vga::height() const {
    return mode_ == 3 ? 400 : mode_ == 0x12 ? 480 : mode_ == 0x10 ? 350 : 200;
}
bool Vga::handles_port(std::uint16_t port) const {
    switch (port) {
    case 0x3c0:
    case 0x3c1:
    case 0x3c2:
    case 0x3c4:
    case 0x3c5:
    case 0x3c6:
    case 0x3c7:
    case 0x3c8:
    case 0x3c9:
    case 0x3cc:
    case 0x3ce:
    case 0x3cf:
    case 0x3d4:
    case 0x3d5:
    case 0x3da:
        return true;
    default:
        return false;
    }
}
std::uint8_t Vga::in(std::uint16_t port) {
    switch (port) {
    case 0x3c1:
        return attr_[attr_index_];
    case 0x3c4:
        return seq_index_;
    case 0x3c5:
        return seq_[seq_index_];
    case 0x3c6:
        return dac_mask_;
    case 0x3c8:
        return dac_write_;
    case 0x3c9: {
        auto value = dac_[dac_read_][dac_component_++];
        if (dac_component_ == 3) {
            dac_component_ = 0;
            ++dac_read_;
        }
        return value;
    }
    case 0x3cc:
        return misc_;
    case 0x3ce:
        return gc_index_;
    case 0x3cf:
        return gc_[gc_index_];
    case 0x3d4:
        return crtc_index_;
    case 0x3d5:
        return crtc_[crtc_index_];
    case 0x3da:
        attr_data_ = false;
        // Bounded synthetic retrace transitions until a guest timer is connected.
        return (++status_reads_ & 1) ? 0 : 9;
    default:
        throw std::runtime_error("unsupported VGA port read");
    }
}
void Vga::out(std::uint16_t port, std::uint8_t value) {
    switch (port) {
    case 0x3c0:
        if (!attr_data_)
            attr_index_ = value & 31;
        else
            attr_[attr_index_] = value;
        attr_data_ = !attr_data_;
        break;
    case 0x3c2:
        misc_ = value;
        break;
    case 0x3c4:
        seq_index_ = value;
        break;
    case 0x3c5:
        seq_[seq_index_] = value;
        break;
    case 0x3c6:
        dac_mask_ = value;
        break;
    case 0x3c7:
        dac_read_ = value;
        dac_component_ = 0;
        break;
    case 0x3c8:
        dac_write_ = value;
        dac_component_ = 0;
        break;
    case 0x3c9:
        dac_[dac_write_][dac_component_++] = value & 63;
        if (dac_component_ == 3) {
            dac_component_ = 0;
            ++dac_write_;
        }
        break;
    case 0x3ce:
        gc_index_ = value;
        break;
    case 0x3cf:
        gc_[gc_index_] = value;
        break;
    case 0x3d4:
        crtc_index_ = value;
        break;
    case 0x3d5:
        crtc_[crtc_index_] = value;
        break;
    default:
        throw std::runtime_error("unsupported VGA port write");
    }
}
std::uint8_t Vga::read(std::uint16_t offset) {
    if (seq_[4] & 8)
        return planes_[offset & 3][offset >> 2];
    for (unsigned p = 0; p < 4; ++p)
        latch_[p] = planes_[p][offset];
    if (!(gc_[5] & 8))
        return latch_[gc_[4] & 3];
    std::uint8_t result = 255;
    for (unsigned p = 0; p < 4; ++p) {
        if (gc_[7] & (1 << p))
            result &= latch_[p] ^ ((gc_[2] & (1 << p)) ? 0 : 255);
    }
    return result;
}
void Vga::write(std::uint16_t offset, std::uint8_t value) {
    if (seq_[4] & 8) {
        if (seq_[2] & (1 << (offset & 3)))
            planes_[offset & 3][offset >> 2] = value;
        return;
    }
    const auto rotated =
        static_cast<std::uint8_t>((value >> (gc_[3] & 7)) | (value << ((8 - (gc_[3] & 7)) & 7)));
    for (unsigned p = 0; p < 4; ++p) {
        if (!(seq_[2] & (1 << p)))
            continue;
        const auto mode = gc_[5] & 3;
        if (mode == 1) {
            planes_[p][offset] = latch_[p];
            continue;
        }
        const bool set = gc_[0] & (1 << p);
        std::uint8_t source = mode == 2                            ? ((value & (1 << p)) ? 255 : 0)
                              : (mode == 3 || (gc_[1] & (1 << p))) ? (set ? 255 : 0)
                                                                   : rotated;
        std::uint8_t mask = gc_[8];
        if (mode == 3)
            mask &= rotated;
        switch ((gc_[3] >> 3) & 3) {
        case 1:
            source &= latch_[p];
            break;
        case 2:
            source |= latch_[p];
            break;
        case 3:
            source ^= latch_[p];
            break;
        }
        planes_[p][offset] = (source & mask) | (latch_[p] & ~mask);
    }
}
void Vga::draw_glyph(unsigned column, unsigned row, std::span<const std::uint8_t> bitmap,
                     std::uint8_t attribute) {
    if (mode_ == 3 || mode_ == 0x13)
        throw std::runtime_error("unsupported BIOS glyph mode");
    const auto start = ((std::uint16_t(crtc_[0x0c]) << 8) | crtc_[0x0d]) * 2u;
    const auto stride = unsigned(crtc_[0x13]) * 2;
    for (unsigned y = 0; y < bitmap.size(); ++y)
        for (unsigned x = 0; x < 8; ++x) {
            const unsigned px = column * 8 + x, py = row * bitmap.size() + y;
            if (px >= unsigned(width()) || py >= unsigned(height()))
                continue;
            const auto offset = (start + py * stride + px / 8) & 0xffff;
            const std::uint8_t bit = 0x80 >> (px & 7);
            const bool ink = bitmap[y] & (0x80 >> x);
            for (unsigned p = 0; p < 4; ++p) {
                if (attribute & 0x80) {
                    if (ink && (attribute & (1 << p)))
                        planes_[p][offset] ^= bit;
                } else {
                    planes_[p][offset] &= ~bit;
                    if (ink && (attribute & (1 << p)))
                        planes_[p][offset] |= bit;
                }
            }
        }
}

void Vga::text_font(unsigned character, std::span<const std::uint8_t> bitmap) {
    if (character >= text_font_.size() || bitmap.empty() || bitmap.size() > 16)
        throw std::runtime_error("invalid text glyph");
    for (unsigned row = 0; row < 16; ++row)
        text_font_[character][row] = bitmap[row * bitmap.size() / 16];
}
void Vga::text_snapshot(std::span<const std::uint8_t> memory) {
    if (memory.size() != text_.size())
        throw std::runtime_error("invalid text memory snapshot");
    std::copy(memory.begin(), memory.end(), text_.begin());
}
void Vga::text_page(unsigned page) {
    if (page >= 8)
        throw std::runtime_error("invalid text page");
    text_page_ = page;
}
void Vga::graphics_page(unsigned page) {
    if (mode_ == 3)
        throw std::runtime_error("graphics page in text mode");
    const unsigned bytes_per_page = page_size();
    if (page >= 65536 / bytes_per_page)
        throw std::runtime_error("invalid graphics page");
    graphics_page_ = page;
    // Planar modes use CRTC word addresses. Mode 13h only has one page.
    const unsigned start = page * bytes_per_page / 2;
    crtc_[0x0c] = start >> 8;
    crtc_[0x0d] = start;
}
unsigned Vga::page_size() const {
    switch (mode_) {
    case 3: return 4096;
    case 0x0d: return 8192;
    case 0x0e: return 16384;
    case 0x10: return 32768;
    case 0x12:
    case 0x13: return 65536;
    default: throw std::runtime_error("unsupported VGA page size");
    }
}
void Vga::attribute_palette(std::span<const std::uint8_t, 17> values) {
    std::copy_n(values.begin(), 16, attr_.begin());
    attr_[0x11] = values[16];
}

void Vga::palette(std::uint16_t index, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    if (index >= dac_.size())
        throw std::runtime_error("VGA palette index out of range");
    dac_[index] = {static_cast<std::uint8_t>(r & 63), static_cast<std::uint8_t>(g & 63),
                   static_cast<std::uint8_t>(b & 63)};
}
std::vector<std::uint32_t> Vga::pixels() const {
    std::vector<std::uint32_t> output(width() * height());
    const auto crtc_start = (std::uint16_t(crtc_[0x0c]) << 8) | crtc_[0x0d];
    const auto start = mode_ == 0x13 ? crtc_start : crtc_start * 2u;
    const auto stride = unsigned(crtc_[0x13]) * (mode_ == 0x13 ? 8 : 2);
    const unsigned line_compare = unsigned(crtc_[0x18]) |
                                  ((crtc_[0x07] & 0x10) ? 0x100 : 0) |
                                  ((crtc_[0x09] & 0x40) ? 0x200 : 0);
    for (int y = 0; y < height(); ++y)
        for (int x = 0; x < width(); ++x) {
            // VGA line compare starts a second display region from address 0.
            // The second region is useful for fixed status/UI panels while
            // the top page scrolls independently.
            const bool split = unsigned(y) > line_compare;
            const unsigned display_start = split ? 0 : start;
            const unsigned display_row = split ? unsigned(y) - line_compare - 1 : unsigned(y);
            unsigned color = 0;
            if (mode_ == 3) {
                const auto cell = text_page_ * 4096 + ((y / 16) * 80 + x / 8) * 2;
                const auto character = text_[cell], attribute = text_[cell + 1];
                const bool ink = text_font_[character][y % 16] & (0x80 >> (x % 8));
                color = attr_[ink ? attribute & 15 : (attribute >> 4) & 7] & 63;
            } else if (mode_ == 0x13) {
                const auto offset = (display_start * 4 + display_row * stride + x) & 0xffff;
                color = planes_[offset & 3][offset >> 2];
            } else {
                const auto offset = (display_start + display_row * stride + x / 8) & 0xffff;
                for (unsigned p = 0; p < 4; ++p)
                    color |= ((planes_[p][offset] >> (7 - (x & 7))) & 1) << p;
                color = attr_[color] & 63;
                if (attr_[0x10] & 0x80)
                    color = (color & 15) | ((attr_[0x14] & 3) << 4);
                color |= (attr_[0x14] & 12) << 4;
            }
            const auto &rgb = dac_[color & dac_mask_];
            auto expand = [](unsigned v) { return (v << 2) | (v >> 4); };
            output[y * width() + x] =
                0xff000000 | (expand(rgb[0]) << 16) | (expand(rgb[1]) << 8) | expand(rgb[2]);
        }
    return output;
}
} // namespace gateway
