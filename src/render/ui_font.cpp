#include "render/ui_font.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#pragma warning(push, 0)
#include <stb_truetype.h>
#pragma warning(pop)

namespace od::ui {

struct Font::Impl {
    stbtt_fontinfo info{};
};

Font::Font() : impl_(std::make_unique<Impl>()) {}
Font::~Font() = default;

bool Font::load(const std::filesystem::path& ttf) {
    std::ifstream f(ttf, std::ios::binary);
    if (!f) return false;
    data_.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    if (data_.empty() || !stbtt_InitFont(&impl_->info, data_.data(), stbtt_GetFontOffsetForIndex(data_.data(), 0))) {
        data_.clear();
        return false;
    }
    reset();
    return true;
}

bool Font::has(uint32_t cp) const { return loaded() && stbtt_FindGlyphIndex(&impl_->info, int(cp)) != 0; }

float Font::emForCapHeight(float cap) const {
    if (!loaded()) return cap;
    int x0, y0, x1, y1; // the capital H's height in font units, per em
    if (!stbtt_GetCodepointBox(&impl_->info, 'H', &x0, &y0, &x1, &y1) || y1 <= 0) return cap / 0.7f;
    const float unitsPerEm = 1.0f / stbtt_ScaleForMappingEmToPixels(&impl_->info, 1.0f);
    return cap * unitsPerEm / float(y1);
}

float Font::kern(uint32_t a, uint32_t b, int px) const {
    if (!loaded()) return 0.0f;
    const stbtt_fontinfo* fi = &impl_->info;
    return float(stbtt_GetCodepointKernAdvance(fi, int(a), int(b))) * stbtt_ScaleForMappingEmToPixels(fi, float(px));
}

void Font::reset() {
    atlas_.assign(size_t(kAtlas) * kAtlas, 0x00ffffffu);
    glyphs_.clear();
    shelfX_ = shelfY_ = shelfH_ = 0;
    dirty = true;
}

const Font::Glyph& Font::glyph(uint32_t cp, int px, int outline) {
    static const Glyph kNone{};
    if (!loaded() || px <= 0 || px > 512) return kNone;
    outline = std::clamp(outline, 0, 32);
    const uint64_t key = uint64_t(cp) << 24 | uint64_t(px) << 8 | uint64_t(outline);
    if (const auto it = glyphs_.find(key); it != glyphs_.end()) return it->second;

    const stbtt_fontinfo* fi = &impl_->info;
    const int gi = stbtt_FindGlyphIndex(fi, int(cp));
    const float scale = stbtt_ScaleForMappingEmToPixels(fi, float(px));
    int adv = 0, lsb = 0, bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
    stbtt_GetGlyphHMetrics(fi, gi, &adv, &lsb);
    stbtt_GetGlyphBitmapBox(fi, gi, scale, scale, &bx0, &by0, &bx1, &by1);
    Glyph g;
    g.ok = gi != 0;
    g.advance = float(adv) * scale;
    const int pad = outline + 1; // a clear texel around it: the bilinear sampler does not pick up the neighbours
    const int w = bx1 - bx0 + 2 * pad, h = by1 - by0 + 2 * pad;
    if (!g.ok || bx1 <= bx0 || by1 <= by0) return glyphs_.emplace(key, g).first->second; // e.g. the space
    if (w > kAtlas || h > kAtlas) return kNone;
    if (shelfX_ + w > kAtlas) { // the next shelf
        shelfX_ = 0;
        shelfY_ += shelfH_;
        shelfH_ = 0;
    }
    if (shelfY_ + h > kAtlas) { // full: start over (the glyphs of this frame are rasterized again)
        reset();
        return glyph(cp, px, outline);
    }
    std::vector<uint8_t> cov(size_t(w) * size_t(h), 0);
    stbtt_MakeGlyphBitmap(fi, &cov[size_t(pad) * size_t(w) + size_t(pad)], bx1 - bx0, by1 - by0, w, scale, scale, gi);
    if (outline > 0) { // grown by the disc of radius r (its rim antialiased)
        std::vector<uint8_t> grown(cov.size(), 0);
        const float r = float(outline);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                float best = 0.0f;
                for (int dy = -outline - 1; dy <= outline + 1; ++dy) {
                    const int sy = y + dy;
                    if (sy < 0 || sy >= h) continue;
                    for (int dx = -outline - 1; dx <= outline + 1; ++dx) {
                        const int sx = x + dx;
                        if (sx < 0 || sx >= w) continue;
                        const float c = cov[size_t(sy) * size_t(w) + size_t(sx)];
                        if (c <= best) continue;
                        const float wgt = std::clamp(r + 0.5f - std::sqrt(float(dx * dx + dy * dy)), 0.0f, 1.0f);
                        best = std::max(best, c * wgt);
                    }
                }
                grown[size_t(y) * size_t(w) + size_t(x)] = uint8_t(best + 0.5f);
            }
        cov.swap(grown);
    }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            atlas_[size_t(shelfY_ + y) * kAtlas + size_t(shelfX_ + x)] =
                uint32_t(cov[size_t(y) * size_t(w) + size_t(x)]) << 24 | 0x00ffffffu;
    g.x0 = float(bx0 - pad);
    g.y0 = float(by0 - pad);
    g.x1 = float(bx1 + pad);
    g.y1 = float(by1 + pad);
    g.u0 = float(shelfX_) / kAtlas;
    g.v0 = float(shelfY_) / kAtlas;
    g.u1 = float(shelfX_ + w) / kAtlas;
    g.v1 = float(shelfY_ + h) / kAtlas;
    shelfX_ += w;
    shelfH_ = std::max(shelfH_, h);
    dirty = true;
    return glyphs_.emplace(key, g).first->second;
}

} // namespace od::ui
