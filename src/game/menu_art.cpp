// OpenDash's own art for the status sub-screen (see menu_art.hpp): the PC font's glyphs halved and a caption cell drawn
// into the VRAM with them.
#include "game/menu_art.hpp"

#include <algorithm>

namespace od::menuart {

// glyph g of the PC message font, halved (each texel the mean of its 2 x 2 block)
Glyph pcGlyph(std::span<const uint8_t> font, uint32_t g) {
    constexpr int kSize = 24, kHalf = kSize / 2;
    Glyph out;
    out.w = out.h = kHalf;
    out.advance = kHalf;
    out.level.assign(size_t(kHalf * kHalf), 0);
    const size_t base = size_t(g) * kSize * kSize / 2;
    if (base + kSize * kSize / 2 > font.size()) return out;
    auto at = [&](int x, int y) {
        const uint8_t b = font[base + size_t(y * kSize + x) / 2];
        return (x & 1) ? b >> 4 : b & 0xf;
    };
    for (int y = 0; y < kHalf; ++y)
        for (int x = 0; x < kHalf; ++x)
            out.level[size_t(y * kHalf + x)] =
                uint8_t((at(x * 2, y * 2) + at(x * 2 + 1, y * 2) + at(x * 2, y * 2 + 1) + at(x * 2 + 1, y * 2 + 1) + 2) / 4);
    return out;
}

// the caption cell: the box (index 3) inside a 1-texel clear border, the glyphs centred (index 6 the glyph, 4 its edge),
// packed 4 texels per VRAM halfword into the tpage's cell
void caption(Vram& vram, uint16_t tpage, int u, int v, int w, const std::vector<Glyph>& glyphs) {
    constexpr int kH = 16;
    std::vector<uint8_t> idx(size_t(w * kH), 0);
    for (int y = 1; y < kH - 1; ++y)
        for (int x = 1; x < w - 1; ++x) idx[size_t(y * w + x)] = 3;
    int tw = 0, th = 0;
    for (const Glyph& g : glyphs) {
        tw += g.advance;
        th = std::max(th, g.h);
    }
    int gx = (w - tw) / 2;
    const int gy = (kH - th) / 2;
    for (const Glyph& g : glyphs) {
        for (int y = 0; y < g.h; ++y)
            for (int x = 0; x < g.w; ++x) {
                const int px = gx + x, py = gy + y;
                if (px < 1 || px >= w - 1 || py < 1 || py >= kH - 1) continue;
                const uint8_t l = g.level[size_t(y * g.w + x)];
                if (l >= 6) idx[size_t(py * w + px)] = 6;
                else if (l >= 3 && idx[size_t(py * w + px)] != 6) idx[size_t(py * w + px)] = 4;
            }
        gx += g.advance;
    }
    // 4 texels per VRAM halfword (low nibble first) at the page's x (tpage & 0xf) * 64, y (tpage >> 4 & 1) * 256
    const int px = (tpage & 0xf) * 64 + u / 4, py = ((tpage >> 4) & 1) * 256 + v;
    std::vector<uint8_t> raw(size_t(w / 4 * kH * 2));
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < w; x += 4) {
            uint16_t hw = 0;
            for (int k = 0; k < 4; ++k) hw = uint16_t(hw | (idx[size_t(y * w + x + k)] & 0xf) << (k * 4));
            raw[size_t((y * (w / 4) + x / 4) * 2)] = uint8_t(hw);
            raw[size_t((y * (w / 4) + x / 4) * 2 + 1)] = uint8_t(hw >> 8);
        }
    vram.loadImage(px, py, w / 4, kH, raw);
}

} // namespace od::menuart
