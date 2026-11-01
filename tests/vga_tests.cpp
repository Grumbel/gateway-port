#include "vga.hpp"
#include <iostream>
#include <stdexcept>
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void gc(gateway::Vga &vga, unsigned index, unsigned value) {
    vga.out(0x3ce, index);
    vga.out(0x3cf, value);
}
void mask(gateway::Vga &vga, unsigned value) {
    vga.out(0x3c4, 2);
    vga.out(0x3c5, value);
}
int main() {
    try {
        gateway::Vga vga;
        vga.set_mode(0x12);
        require(vga.width() == 640 && vga.height() == 480, "mode 12 dimensions");
        mask(vga, 1);
        vga.write(0, 0x80);
        auto pixels = vga.pixels();
        require(pixels[0] == 0xff0000aa && pixels[1] == 0xff000000,
                "plane bits not decoded from most significant bit");
        mask(vga, 4);
        vga.write(80, 0x80);
        require(vga.pixels()[640] == 0xffaa0000, "planar pitch incorrect");
        // Latches retain a complete source byte across write-mode-1 copies.
        vga.read(0);
        gc(vga, 5, 1);
        mask(vga, 15);
        vga.write(1, 0);
        require(vga.pixels()[8] == 0xff0000aa, "latch copy failed");
        // Mode 2 expands a four-bit pixel color into plane bytes.
        gc(vga, 5, 2);
        vga.write(2, 6);
        require(vga.pixels()[16] == 0xffaa5500, "mode 2 color expansion failed");
        // Mode 0 set/reset with a bit mask must preserve the read latch.
        gc(vga, 5, 0);
        vga.read(2);
        gc(vga, 0, 1);
        gc(vga, 1, 15);
        gc(vga, 8, 0x80);
        vga.write(2, 0);
        pixels = vga.pixels();
        require(pixels[16] == 0xff0000aa && pixels[17] == 0xffaa5500,
                "set/reset bit-mask merge failed");
        // Read-mode-1 color comparison uses each enabled plane.
        gc(vga, 5, 8);
        gc(vga, 2, 1);
        gc(vga, 7, 15);
        require(vga.read(2) == 0x80, "color comparison failed");
        // Write mode 3 applies the rotated host mask to set/reset.
        gc(vga, 5, 3);
        gc(vga, 0, 4);
        gc(vga, 8, 255);
        vga.read(2);
        vga.write(2, 0x40);
        require(vga.pixels()[17] == 0xffaa0000, "mode 3 mask failed");
        // Palette DAC stream and attribute mapping must affect the decoded color.
        vga.out(0x3c8, 1);
        vga.out(0x3c9, 63);
        vga.out(0x3c9, 0);
        vga.out(0x3c9, 0);
        require(vga.pixels()[0] == 0xffff0000, "DAC write not displayed");
        vga.out(0x3c7, 1);
        require(vga.in(0x3c9) == 63 && vga.in(0x3c9) == 0, "DAC read sequencing failed");
        // Changing the display start exposes the following byte, not a fresh buffer.
        vga.out(0x3d4, 0x0d);
        vga.out(0x3d5, 1);
        require(vga.pixels()[0] == 0xffff0000, "display start ignored");
        std::array<std::uint8_t, 16> glyph{};
        glyph[0] = 0x80;
        vga.draw_glyph(2, 0, glyph, 4);
        require(vga.pixels()[16] == 0xffaa0000 && vga.pixels()[17] == 0xff000000,
                "BIOS glyph foreground/background failed");
        vga.draw_glyph(2, 0, glyph, 0x84);
        require(vga.pixels()[16] == 0xff000000, "BIOS XOR glyph failed");
        vga.set_mode(0x13);
        vga.write(0, 4);
        vga.write(1, 2);
        pixels = vga.pixels();
        require(pixels[0] == 0xffaa0000 && pixels[1] == 0xff00aa00, "chain-four addressing failed");
        vga.set_mode(0x10);
        require(vga.width() == 640 && vga.height() == 350, "mode 10 dimensions");
        mask(vga, 1);
        vga.write(28000, 0x80);
        vga.graphics_page(1);
        require(vga.graphics_page() == 1 && vga.pixels()[0] == 0xff0000aa,
                "graphics page selection ignored");
        vga.write(28160, 0x80);
        // Compare at scanline 1: the next line must restart at address zero,
        // rather than continue two scanlines into page one.
        vga.out(0x3d4, 0x18);
        vga.out(0x3d5, 1);
        vga.out(0x3d4, 0x07);
        vga.out(0x3d5, 0);
        vga.out(0x3d4, 0x09);
        vga.out(0x3d5, 0);
        require(vga.pixels()[2 * 640] == 0xff000000, "line compare ignored");
        vga.write(0, 0x80);
        require(vga.pixels()[2 * 640] == 0xff0000aa, "line compare did not reset address");
        vga.graphics_page(0);
        require(vga.pixels()[0] == 0xff0000aa, "graphics page zero not restored");
        vga.set_mode(3);
        require(vga.width() == 640 && vga.height() == 400, "80x25 text dimensions");
        std::array<std::uint8_t, 8> font{};
        font[0] = 0x80;
        vga.text_font('G', font);
        std::array<std::uint8_t, 32768> text{};
        text[0] = 'G';
        text[1] = 0x14;
        text[4096] = 'G';
        text[4097] = 0x8f;
        vga.text_snapshot(text);
        pixels = vga.pixels();
        require(pixels[0] == 0xffaa0000 && pixels[640] == 0xffaa0000 && pixels[1] == 0xff0000aa,
                "text font scaling or foreground/background failed");
        vga.text_page(1);
        require(vga.pixels()[0] == 0xffffffff && vga.pixels()[1] == 0xff000000,
                "text page or blink attribute treated as graphics XOR");
        vga.set_mode(3, false);
        require(vga.text_memory()[0] == 'G', "no-clear mode set erased text");
        vga.set_mode(3);
        require(vga.text_memory()[0] == ' ' && vga.text_memory()[1] == 7,
                "text clear did not restore blank cells");
        vga.set_mode(0x12);
        mask(vga, 1);
        vga.write(0, 0x80);
        std::array<std::uint8_t, 17> ega_palette{};
        ega_palette[1] = 4;
        ega_palette[16] = 7;
        vga.attribute_palette(ega_palette);
        require(vga.pixels()[0] == 0xffaa0000, "BIOS attribute palette mapping ignored");
        std::cout << "VGA checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
