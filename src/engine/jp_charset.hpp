// The Unicode text of the PC game's Japanese glyphs (INIT.DAT's type-7 fonts): font 0 = the 834 glyphs of 24x24 the
// text draws at 12 px (0..31 marks, 32..127 ASCII, 128..288 kana, 289.. kanji), font 1 = the 804 glyphs of 48x48
// drawn at 24 px (digits, kana, kanji). A script's glyph byte b < 0xd0 is font 0 glyph b, d0..df xx font 0 glyph
// (b - 0xd0) << 8 | xx, e0..fa xx font 1 glyph (b - 0xe0) << 8 | xx. 0 / "" = a glyph with no Unicode form (the
// hands, the empty cells): drawn from the bitmap.
#pragma once

#include <cstdint>
#include <string>

namespace od::lang {

uint32_t jpCodepoint(int font, int glyph);       // one code point per glyph; 0 = none
std::string jpGlyphText(int font, int glyph);    // the same as UTF-8
int jpGlyphOf(uint32_t codepoint);               // the font 0 glyph of a code point (0x20 the space); -1 = none

} // namespace od::lang
