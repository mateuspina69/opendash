// OpenDash's own art for the status sub-screen (rows the exe has no picture for: the language row), drawn into the
// sub-screen's VRAM in the style of the exe's captions (SUBSCN06 tpage 7, cluts 0x7f12 / 0x7f13 / 0x7f50 / 0x7f51:
// index 0 clear, 3 the box, 4 the glyph edge, 6 the glyph; a 1-texel clear border) with the game's own glyphs.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "engine/vram.hpp"

namespace od::menuart {

struct Glyph {
    int w = 0, h = 0;            // the bitmap
    int advance = 0;             // texels to the next glyph
    std::vector<uint8_t> level;  // w * h, 0..15
};

// glyph g of the PC message font (INIT.DAT type-7 slot 0: 24 x 24, 4 bits per texel, low nibble first), halved to
// 12 x 12 as the game draws it on the 320 x 240 screen (advance 12)
Glyph pcGlyph(std::span<const uint8_t> font, uint32_t g);

// a caption cell at (u, v) of the 4-bit tpage `tpage`: w x 16, the box with the glyphs centred
void caption(Vram& vram, uint16_t tpage, int u, int v, int w, const std::vector<Glyph>& glyphs);

} // namespace od::menuart
