#include "app/ui/ui_text.hpp"

#include <SDL3/SDL.h>

#include <cmath>

namespace od::app {

uint32_t nextCodepoint(std::string_view s, size_t& i) {
    if (i >= s.size()) return 0;
    const auto b = [&](size_t k) { return uint32_t(static_cast<unsigned char>(s[k])); };
    const uint32_t c = b(i);
    if (c < 0x80) return ++i, c;
    if ((c & 0xe0) == 0xc0 && i + 1 < s.size()) {
        const uint32_t v = (c & 0x1f) << 6 | (b(i + 1) & 0x3f);
        i += 2;
        return v;
    }
    if ((c & 0xf0) == 0xe0 && i + 2 < s.size()) {
        const uint32_t v = (c & 0x0f) << 12 | (b(i + 1) & 0x3f) << 6 | (b(i + 2) & 0x3f);
        i += 3;
        return v;
    }
    ++i;
    return 0xfffd;
}

bool UiText::load(od::Renderer& r, const std::filesystem::path& ttf) {
    tried = true;
    if (!font.load(ttf)) {
        SDL_Log("ui text: cannot read %s", ttf.string().c_str());
        return false;
    }
    tex = r.createTexture(ui::Font::kAtlas, ui::Font::kAtlas, font.atlas().data());
    font.dirty = false;
    return tex != 0;
}

void UiText::flush(od::Renderer& r) {
    if (!tex || !font.dirty) return;
    r.updateTexture(tex, font.atlas().data());
    font.dirty = false;
}

float UiText::width(std::string_view text, float px) {
    float w = 0.0f;
    const int ipx = int(std::lround(px));
    for (size_t i = 0; i < text.size();)
        if (const uint32_t cp = nextCodepoint(text, i)) w += font.glyph(cp, ipx, 0).advance;
    return w;
}

void UiText::draw(ui::Canvas& cv, std::string_view text, float x, float y, float px, ui::Color col, float edge,
                  ui::Color edgeCol) {
    if (!tex) return;
    const int ipx = int(std::lround(px)), ie = edge > 0.0f ? std::max(1, int(std::lround(edge))) : 0;
    x = std::floor(x + 0.5f);
    y = std::floor(y + 0.5f);
    auto put = [&](const ui::Font::Glyph& g, float pen, ui::Color c) {
        cv.image(tex, ui::Vec2(pen + g.x0, y + g.y0), ui::Vec2(pen + g.x1, y + g.y1), ui::Vec2(g.u0, g.v0),
                 ui::Vec2(g.u1, g.v1), c);
    };
    if (ie) { // the outlines first: a glyph's outline never covers its neighbour's fill
        float pen = x;
        for (size_t i = 0; i < text.size();) {
            const uint32_t cp = nextCodepoint(text, i);
            const ui::Font::Glyph g = font.glyph(cp, ipx, ie);
            if (g.ok) put(g, pen, edgeCol);
            pen += font.glyph(cp, ipx, 0).advance;
        }
    }
    float pen = x;
    for (size_t i = 0; i < text.size();) {
        const uint32_t cp = nextCodepoint(text, i);
        const ui::Font::Glyph g = font.glyph(cp, ipx, 0);
        if (g.ok) put(g, pen, col);
        pen += g.advance;
    }
}

} // namespace od::app
