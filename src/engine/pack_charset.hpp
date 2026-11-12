// The glyphs of a language pack in the PC's format (engine/lang pcScript: Korean, Chinese): font 0 glyphs from
// kPackGlyphBase on (script bytes d4..df xx) have no bitmap; their code points are the pack's charset.bin (u32 each,
// little endian, glyph kPackGlyphBase + i) and the HD text draws them from the pack's vector font.
#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace od::lang {

// OpenDash's fixed glyphs (any language): the names of the Korean and Chinese texts in the options' language menu,
// drawn from the Korean vector font (it has the hanja too); a pack's charset stops before kFixedGlyphBase
inline constexpr int kFixedGlyphBase = 0xff0;
inline constexpr uint16_t kKoreanName[] = {0xff0, 0xff1, 0xff2, 0}; // hangugeo
inline constexpr uint16_t kChineseName[] = {0xff3, 0xff4, 0};       // zhongwen
inline constexpr uint16_t kChineseHKName[] = {0xffb, 0xffc, 0xff3, 0xff4, 0}; // fanti zhongwen
inline constexpr uint16_t kRussianName[] = {0xff5, 0xff6, 0xff7, 0xff7, 0xff8, 0xff9, 0xffa, 0}; // Russkiy
uint32_t fixedCodepoint(int g); // 0 = not one of them

// the active pack's table (empty: none)
void setPackGlyphs(std::vector<uint32_t> codepoints);
bool packGlyphsActive();
// OpenDash: a pack of an alphabet (Russian): its glyphs set at the vector font's own advance, not in 12 px cells
// (font.txt's second line `proportional`); the option markers keep the game's 12 px columns
void setPackProportional(bool on);
bool packProportional();
// the code point of font 0 glyph g from the pack's table; 0 = not one of the pack's
uint32_t packCodepoint(int g);
// the pack's glyph of a code point; -1 = none
int packGlyphOf(uint32_t cp);
// the glyph of a code point for the PC's text: the pack's, else the PC font's (jp_charset), -1 = none
int pcGlyphOf(uint32_t cp);
// UTF-8 -> PC glyph codes (pcGlyphOf; characters without a glyph are dropped)
std::vector<uint16_t> pcEncode(std::string_view utf8);

} // namespace od::lang
