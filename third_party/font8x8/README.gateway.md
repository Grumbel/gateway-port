# font8x8

`font8x8_basic.h` is Daniel Hepper's public-domain basic Latin bitmap font:
https://github.com/dhepper/font8x8/blob/master/font8x8_basic.h

Retrieved 2026-10-31. The original attribution and public-domain notice remain
in the header. The declaration is changed to `static constexpr unsigned char`
for C++ inclusion; glyph data is unchanged.

The runtime reverses each row's bit order for VGA and scales the eight rows to
the requested BIOS cell height. This supplies ASCII when a game has no fixed
font resource; it is not a complete CP437 or native VGA 8x14/8x16 ROM font.
