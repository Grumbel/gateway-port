#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace gateway {
// Conventional VGA planar graphics and 80x25 text presentation.
class Vga {
  public:
    Vga();
    void set_mode(std::uint8_t mode, bool clear = true);
    std::uint8_t mode() const { return mode_; }
    int width() const;
    int height() const;
    bool handles_port(std::uint16_t port) const;
    std::uint8_t in(std::uint16_t port);
    void out(std::uint16_t port, std::uint8_t value);
    std::uint8_t read(std::uint16_t offset);
    void write(std::uint16_t offset, std::uint8_t value);
    std::vector<std::uint32_t> pixels() const; // ARGB8888, opaque alpha.
    void draw_glyph(unsigned column, unsigned row, std::span<const std::uint8_t> bitmap,
                    std::uint8_t attribute, unsigned page = 0);
    void scroll_graphics_window(bool down, unsigned lines, std::uint8_t color,
                                unsigned top, unsigned left, unsigned bottom, unsigned right);
    void text_font(unsigned character, std::span<const std::uint8_t> bitmap);
    void text_snapshot(std::span<const std::uint8_t> memory);
    std::span<const std::uint8_t> text_memory() const { return text_; }
    void text_page(unsigned page);
    unsigned text_page() const { return text_page_; }
    void graphics_page(unsigned page);
    unsigned graphics_page() const { return graphics_page_; }
    unsigned page_size() const;
    void attribute_palette(std::span<const std::uint8_t, 17> values);
    void palette(std::uint16_t index, std::uint8_t r, std::uint8_t g, std::uint8_t b);

  private:
    std::uint8_t mode_ = 3;
    unsigned text_page_ = 0;
    unsigned graphics_page_ = 0;
    std::array<std::uint8_t, 32768> text_{};
    std::array<std::array<std::uint8_t, 16>, 256> text_font_{};
    std::array<std::array<std::uint8_t, 65536>, 4> planes_{};
    std::array<std::uint8_t, 4> latch_{};
    std::array<std::uint8_t, 256> seq_{}, gc_{}, crtc_{}, attr_{};
    std::array<std::array<std::uint8_t, 3>, 256> dac_{};
    std::uint8_t seq_index_ = 0, gc_index_ = 0, crtc_index_ = 0, attr_index_ = 0;
    bool attr_data_ = false;
    std::uint8_t dac_read_ = 0, dac_write_ = 0, dac_component_ = 0, dac_mask_ = 255;
    std::uint8_t misc_ = 0x63;
    unsigned status_reads_ = 0;
};
} // namespace gateway
