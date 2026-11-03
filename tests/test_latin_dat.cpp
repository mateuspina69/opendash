// engine/lang latinDatWidths: a proportional Latin font 0 in INIT.DAT is told from the PC's own, and its advances read.
#include <cstdint>
#include <vector>

#include "check.hpp"
#include "engine/lang.hpp"

namespace {

constexpr int kCell = 24;
constexpr size_t kGlyphBytes = size_t(kCell / 2) * kCell;

// glyph g with full ink in columns [first, last] on one row
void inkColumns(std::vector<uint8_t>& font, int g, int first, int last) {
    for (int x = first; x <= last; ++x) {
        uint8_t& b = font[size_t(g) * kGlyphBytes + size_t(5 * kCell + x) / 2];
        b |= (x & 1) ? 0xf0 : 0x0f;
    }
}

std::vector<uint8_t> fontWithLetters(int first, int last) {
    std::vector<uint8_t> font(kGlyphBytes * 256, 0);
    for (int g = 'A'; g <= 'Z'; ++g) inkColumns(font, g, first, last);
    for (int g = 'a'; g <= 'z'; ++g) inkColumns(font, g, first, last);
    return font;
}

} // namespace

int main() {
    using od::lang::latinDatWidths;
    { // flush left: the advance is the ink's right edge in 12 px cell pixels
        std::vector<uint8_t> font = fontWithLetters(2, 13);
        font[size_t('i') * kGlyphBytes + size_t(5 * kCell + 4) / 2] = 0; // 'i': columns 2..3 only
        font[size_t('i') * kGlyphBytes + size_t(5 * kCell + 6) / 2] = 0;
        font[size_t('i') * kGlyphBytes + size_t(5 * kCell + 8) / 2] = 0;
        font[size_t('i') * kGlyphBytes + size_t(5 * kCell + 10) / 2] = 0;
        font[size_t('i') * kGlyphBytes + size_t(5 * kCell + 12) / 2] = 0;
        const auto w = latinDatWidths(font, kCell);
        CHECK(w.has_value());
        if (w) {
            CHECK((*w)['a'] == 7);
            CHECK((*w)['E'] == 7);
            CHECK((*w)['i'] == 2);
            CHECK((*w)[' '] == 6);
            CHECK((*w)['5'] == 10);
            CHECK((*w)['?'] == 12);
            CHECK((*w)[0x80] == 12);
        }
    }
    // the letters centred in their cells, as in the PC's own font: not this font
    CHECK(!latinDatWidths(fontWithLetters(7, 17), kCell).has_value());
    // no glyphs, or a cell that does not scale to 12 px
    CHECK(!latinDatWidths({}, kCell).has_value());
    CHECK(!latinDatWidths(fontWithLetters(2, 13), 25).has_value());
    CHECK(od::lang::latinDatDrop('g') == 1);
    CHECK(od::lang::latinDatDrop('y') == 2);
    CHECK(od::lang::latinDatDrop('a') == 0);
    return g_failures != 0;
}
