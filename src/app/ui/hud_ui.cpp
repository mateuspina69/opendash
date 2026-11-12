#include "app/ui/hud_ui.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <stb_image.h>

#include "app/room_mesh.hpp"
#include "app/ui/vector_ui.hpp"
#include "engine/dat.hpp"
#include "engine/data_source.hpp"
#include "engine/vram.hpp"

namespace od::app {

namespace {

// assets/ui/hud/hud_<key as 5 hex digits>.png (e.g. hud_d7fd0.png: tpage 0x0d, clut 0x7fd0): the same 256 x 256 page
// drawn at a higher resolution (square, RGBA; the UVs are normalised, so any size of 256 px or more); 0 = none
od::TextureHandle loadHdPage(od::Renderer& r, uint32_t key) {
    char name[32];
    std::snprintf(name, sizeof name, "hud_%05x.png", unsigned(key));
    const std::filesystem::path path = assetsDir() / "ui" / "hud" / name;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) return 0;
    std::vector<uint8_t> bytes;
    try {
        bytes = od::readFile(path);
    } catch (const std::exception&) {
        return 0;
    }
    int w = 0, h = 0, comp = 0;
    stbi_uc* px = stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h, &comp, 4);
    if (!px) {
        SDL_Log("hud: cannot read %s", path.string().c_str());
        return 0;
    }
    od::TextureHandle t = 0;
    if (w == h && w >= 256) t = r.createTexture(w, h, reinterpret_cast<const uint32_t*>(px));
    else SDL_Log("hud: %s is %dx%d, not a square page of 256 px or more", path.string().c_str(), w, h);
    stbi_image_free(px);
    if (t) SDL_Log("hud: HD page %s (%dx%d)", name, w, h);
    return t;
}

} // namespace

std::filesystem::path creditPage(const std::string& name, const std::string& lang) {
    std::error_code ec;
    for (const std::string& l : {lang, std::string("en")}) {
        const auto p = assetsDir() / "ui" / "credits" / (name + "_" + l + ".png");
        if (std::filesystem::is_regular_file(p, ec)) return p;
    }
    return {};
}

int creditCards(const std::string& lang) {
    int n = 0;
    char name[8];
    for (;;) {
        std::snprintf(name, sizeof name, "c%02d", n + 1);
        if (creditPage(name, lang).empty()) return n;
        ++n;
    }
}

CreditsFrame creditsFrame(double t, double song, int cards) {
    CreditsFrame f;
    if (cards <= 0) {
        f.done = true;
        return f;
    }
    // (Herbert) the cards spread over the whole song: the background fades in, the cards one by one (fade in, hold,
    // fade out), the last one (PRESENTED BY CAPCOM) held to the song's end, everything faded out with it
    const double D = song > 30.0 ? song : 219.0, intro = 3.0, last = 12.0, fade = 1.2, out = 2.5;
    f.background = float(std::clamp(t / 2.0, 0.0, 1.0) * std::clamp((D - t) / out, 0.0, 1.0));
    if (t >= D) {
        f.done = true;
        return f;
    }
    const double slot = std::max(4.0, (D - intro - last) / double(std::max(1, cards - 1)));
    if (t < intro) return f;
    const int k = int((t - intro) / slot);
    if (k >= cards - 1) { // the last card to the end
        f.card = cards - 1;
        const double u = t - (intro + slot * (cards - 1));
        f.alpha = float(std::clamp(u / fade, 0.0, 1.0) * std::clamp((D - t) / out, 0.0, 1.0));
        return f;
    }
    const double u = t - intro - slot * k, gap = 0.3;
    f.card = k;
    f.alpha = float(std::clamp(std::min(u / fade, (slot - gap - u) / fade), 0.0, 1.0));
    return f;
}

void HudUi::drawCredits(ui::Canvas& cv, od::Renderer& r, const CreditsFrame& f, float width, float height) {
    cv.quadFilled(ui::Vec2(0.0f, 0.0f), ui::Vec2(width, 0.0f), ui::Vec2(width, height), ui::Vec2(0.0f, height),
                  ui::rgba(0, 0, 0, 255));
    float aspect = 4.0f / 3.0f;
    // the game's credits illustration (ST57T), dimmed, covering the window
    const std::filesystem::path bg = assetsDir() / "ui" / "credits" / "fundo.png";
    if (f.background > 0.0f)
        if (const od::TextureHandle t = creditTexture(r, bg, aspect)) {
            const float w = std::max(width, height * aspect), h = w / aspect, x0 = (width - w) * 0.5f, y0 = (height - h) * 0.5f;
            const int v = int(255.0f * 0.45f * f.background);
            cv.imageQuad(t, ui::Vec2(x0, y0), ui::Vec2(x0 + w, y0), ui::Vec2(x0 + w, y0 + h), ui::Vec2(x0, y0 + h),
                         ui::Vec2(0.0f, 0.0f), ui::Vec2(1.0f, 0.0f), ui::Vec2(1.0f, 1.0f), ui::Vec2(0.0f, 1.0f),
                         ui::rgba(v, v, v, 255));
        }
    if (f.card < 0 || f.alpha <= 0.0f) return;
    char name[8];
    std::snprintf(name, sizeof name, "c%02d", f.card + 1);
    const std::filesystem::path page = creditPage(name, creditsLang);
    if (page.empty()) return;
    aspect = 16.0f / 9.0f;
    if (const od::TextureHandle t = creditTexture(r, page, aspect)) { // the window's height, centred
        const float w = height * aspect, x0 = (width - w) * 0.5f;
        const int a = int(255.0f * f.alpha);
        cv.imageQuad(t, ui::Vec2(x0, 0.0f), ui::Vec2(x0 + w, 0.0f), ui::Vec2(x0 + w, height), ui::Vec2(x0, height),
                     ui::Vec2(0.0f, 0.0f), ui::Vec2(1.0f, 0.0f), ui::Vec2(1.0f, 1.0f), ui::Vec2(0.0f, 1.0f),
                     ui::rgba(255, 255, 255, a));
    }
}

int galleryPictures() {
    int n = 0;
    std::error_code ec;
    char name[16];
    for (;;) {
        std::snprintf(name, sizeof name, "g%02d.png", n + 1);
        if (!std::filesystem::is_regular_file(assetsDir() / "ui" / "gallery" / name, ec)) return n;
        ++n;
    }
}

void HudUi::drawGallery(ui::Canvas& cv, od::Renderer& r, const std::filesystem::path& picture, int index, int count,
                        float width, float height, bool side) {
    CreditsFrame f;
    f.background = 0.6f; // (the credits' illustration, dimmed further)
    drawCredits(cv, r, f, width, height);
    if (index < 0 || index >= count) return;
    // the Extras' pictures are many: only the one shown keeps its texture
    for (auto it = creditTex.begin(); it != creditTex.end();) {
        if (it->first.find("extras") != std::string::npos && it->first != picture.string()) {
            r.destroyTexture(it->second.first);
            it = creditTex.erase(it);
        } else {
            ++it;
        }
    }
    float aspect = 1.0f;
    if (const od::TextureHandle t = creditTexture(r, picture, aspect)) {
        // the picture fitted to 78 % of the window's height (and 88 % of its width), above the caption
        float h = height * (side ? 0.56f : 0.78f), w = h * aspect;
        const float maxW = side ? std::min(width * 0.34f, height * 0.6f) : width * 0.88f;
        if (w > maxW) w = maxW, h = w / aspect;
        // (side: its right edge 3 % left of the middle, centred on the text block's height)
        const float x0 = side ? width * 0.5f - height * 0.03f - w : (width - w) * 0.5f;
        const float y0 = side ? height * 0.45f - h * 0.5f : (height * 0.86f - h) * 0.5f, e = std::max(2.0f, height / 360.0f);
        cv.rectFilled(ui::Vec2(x0 - e, y0 - e), ui::Vec2(x0 + w + e, y0 + h + e), ui::rgba(0, 0, 0, 255)); // (the pictures' own black round them)
        cv.imageQuad(t, ui::Vec2(x0, y0), ui::Vec2(x0 + w, y0), ui::Vec2(x0 + w, y0 + h), ui::Vec2(x0, y0 + h),
                     ui::Vec2(0.0f, 0.0f), ui::Vec2(1.0f, 0.0f), ui::Vec2(1.0f, 1.0f), ui::Vec2(0.0f, 1.0f),
                     ui::rgba(255, 255, 255, 255));
        // the arrows at its sides and the dots under it (the one shown lit)
        const float cy = y0 + h * 0.5f, a = height * 0.03f, gap = height * 0.03f;
        const ui::Color arrow = ui::rgba(0x48, 0xd0, 0xd0, 230);
        const ui::Vec2 l[3] = {ui::Vec2(x0 - gap - a, cy), ui::Vec2(x0 - gap, cy - a), ui::Vec2(x0 - gap, cy + a)};
        const float rx = side ? width * 0.5f + height * 0.03f + std::min(width * 0.4f, height * 0.75f) : x0 + w; // (past the text)
        const ui::Vec2 rr[3] = {ui::Vec2(rx + gap + a, cy), ui::Vec2(rx + gap, cy + a), ui::Vec2(rx + gap, cy - a)};
        cv.convexFillAA(l, 3, arrow);
        cv.convexFillAA(rr, 3, arrow);
        const float dy = height * 0.955f, step = height * 0.03f, x = width * 0.5f - step * float(count - 1) * 0.5f;
        for (int k = 0; k < count && count <= 24; ++k) // (a long album: its number in the caption instead)
            cv.disc(ui::Vec2(x + step * float(k), dy), height * (k == index ? 0.008f : 0.005f),
                    k == index ? ui::rgba(0x80, 0xe8, 0xb8, 255) : ui::rgba(0x80, 0x88, 0x98, 200));
    }
}

int creditExtraPages(const std::string& lang) {
    int n = 0;
    char name[8];
    for (;;) {
        std::snprintf(name, sizeof name, "x%02d", n + 1);
        if (creditPage(name, lang).empty()) return n;
        ++n;
    }
}

od::TextureHandle HudUi::creditTexture(od::Renderer& r, const std::filesystem::path& path, float& aspect) {
    const auto key = path.string();
    if (const auto it = creditTex.find(key); it != creditTex.end()) {
        aspect = it->second.second;
        return it->second.first;
    }
    od::TextureHandle t = 0;
    aspect = 16.0f / 9.0f;
    try {
        const std::vector<uint8_t> bytes = od::readFile(path);
        int w = 0, h = 0, comp = 0;
        if (stbi_uc* px = stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h, &comp, 4)) {
            // white letters on black: the black becomes transparent (alpha = the brightest channel, the colour
            // brought back to full), so the page lies over the staff roll's background as the game's pages do; a
            // picture with its own transparency (the cards cNN, outlined) or none at all (the background) is kept
            bool ownAlpha = false;
            for (int i = 0; i < w * h && !ownAlpha; ++i) ownAlpha = px[size_t(i) * 4 + 3] != 255;
            const std::string file = path.filename().string();
            const bool cut = !ownAlpha && (file[0] == 'p' || file[0] == 'x'); // (the game's pages' HD copies)
            for (int i = 0; cut && i < w * h; ++i) {
                uint8_t* c = px + size_t(i) * 4;
                const int m = std::max({c[0], c[1], c[2]});
                if (m > 0)
                    for (int k = 0; k < 3; ++k) c[k] = uint8_t(std::min(255, c[k] * 255 / m));
                c[3] = uint8_t(m);
            }
            t = r.createTexture(w, h, reinterpret_cast<const uint32_t*>(px));
            aspect = float(w) / float(h);
            stbi_image_free(px);
        }
    } catch (const std::exception& e) {
        SDL_Log("credits: %s", e.what());
    }
    creditTex[key] = {t, aspect};
    return t;
}

void HudUi::load(od::Renderer& r) {
    tried = true;
    try {
        od::Vram vram;
        if (g_roomVramGen != 0) {
            vram = g_roomVram; // the room's VRAM (it holds INIT / GAME too)
        } else {
            for (const char* f : {"INIT.DAT", "GAME.DAT"}) vram.apply(od::dataSource().gameFile(f));
        }
        for (auto& [key, t] : tex) r.destroyTexture(t);
        tex.clear();
        vramGen = g_roomVramGen;
        // tpage 0x0d: the HUD frames; 0x0e: the escape timer of 0x4270a0 (content/calbania escapeTimerDraw)
        for (const uint32_t key : {0xd7fd0u, 0xd7fd1u, 0xd7fd2u, 0xd7fd3u, 0xe3c38u, 0xe3c78u, 0x367dc8u, 0x367d48u, 0x367dc0u,
                                   0x367d80u}) {
            if (const od::TextureHandle t = loadHdPage(r, key)) { // an HD page in the assets takes the enlarged one's place
                tex[key] = t;
                continue;
            }
            const auto rgba = vram.decode(uint16_t(key >> 16), uint16_t(key & 0xffff));
            std::vector<uint32_t> px(1024 * 1024);
            for (size_t y = 0; y < 1024; ++y)
                for (size_t x = 0; x < 1024; ++x)
                    std::memcpy(&px[y * 1024 + x], &rgba[((y / 4) * 256 + x / 4) * 4], 4);
            if (const od::TextureHandle t = r.createTexture(1024, 1024, px.data())) tex[key] = t;
        }
    } catch (const std::exception& e) {
        SDL_Log("hud: %s", e.what());
    }
}

namespace {

constexpr int kScreenW = 320, kScreenH = 240, kStaffScale = 4; // nearest-enlarged: the UI's sampler is linear
enum class SpriteBlend { Opaque, Add, Subtract };
struct Rgb5 {
    int r = 0, g = 0, b = 0;
};
// A textured sprite as the GPU draws it: texel 0 is not drawn, the colour scales it (0x80 = as it is), a semi-transparent
// sprite blends only the texels with the mask bit
void drawSprite(std::array<Rgb5, kScreenW * kScreenH>& screen, int pageX, int pageY, int mode, uint16_t clut, int x, int y,
                int u0, int v0, int w, int h, int level, SpriteBlend blend) {
    const od::Vram& vram = g_roomVram;
    const int cx = (clut & 0x3f) * 16, cy = (clut >> 6) & 0x1ff;
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            const int sx = x + i, sy = y + j;
            if (sx < 0 || sy < 0 || sx >= kScreenW || sy >= kScreenH) continue;
            const int u = (u0 + i) & 0xff, v = (v0 + j) & 0xff;
            uint16_t c;
            if (mode == 0) c = vram.pixel(cx + ((vram.pixel(pageX + (u >> 2), pageY + v) >> ((u & 3) * 4)) & 0xf), cy);
            else if (mode == 1) c = vram.pixel(cx + ((vram.pixel(pageX + (u >> 1), pageY + v) >> ((u & 1) * 8)) & 0xff), cy);
            else c = vram.pixel(pageX + u, pageY + v);
            if (c == 0) continue;
            auto scaled = [&](int bits) { return std::min(31, (bits * level) >> 7); };
            const Rgb5 t{scaled(c & 0x1f), scaled((c >> 5) & 0x1f), scaled((c >> 10) & 0x1f)};
            Rgb5& d = screen[size_t(sy) * kScreenW + sx];
            if (blend == SpriteBlend::Opaque || !(c & 0x8000)) d = t;
            else if (blend == SpriteBlend::Add) d = {std::min(31, d.r + t.r), std::min(31, d.g + t.g), std::min(31, d.b + t.b)};
            else d = {std::max(0, d.r - t.r), std::max(0, d.g - t.g), std::max(0, d.b - t.b)};
        }
}

} // namespace

void HudUi::drawStaff(ui::Canvas& cv, od::Renderer& r, const od::port::StaffPicture& pic, float width, float height) {
    if (!pic.background && !pic.page && pic.extra < 0) return;
    // OpenDash: the game's text pages drawn again in HD in the interface's language, its own pages added
    char pageName[8] = "";
    if (pic.extra >= 0) std::snprintf(pageName, sizeof pageName, "x%02d", pic.extra + 1);
    else if (pic.page && pic.pageIndex >= 0) std::snprintf(pageName, sizeof pageName, "p%02d", pic.pageIndex);
    const std::filesystem::path hdPage = pageName[0] ? creditPage(pageName, creditsLang) : std::filesystem::path();
    const bool replaced = !hdPage.empty() && pic.extra < 0;
    const auto same = [](const od::port::StaffPicture& a, const od::port::StaffPicture& b) {
        return a.background == b.background && a.backgroundLevel == b.backgroundLevel && a.page == b.page &&
               a.brightness == b.brightness && a.vramX == b.vramX && a.clutY == b.clutY && a.segment == b.segment &&
               a.decor == b.decor && a.pageIndex == b.pageIndex && a.extra == b.extra;
    };
    if (!staffTex || !same(pic, staffShown)) {
        static std::array<Rgb5, kScreenW * kScreenH> screen;
        screen.fill({});
        // the OT: the background's list (0x88) before the page's (0x84); each list's last packet first
        if (pic.background) {
            drawSprite(screen, 0x380, 0x100, 1, 0x7dc0, 0x100, 0, 0, 0, 0x40, 0xf0, pic.backgroundLevel, SpriteBlend::Opaque); // tpage 0xbe
            drawSprite(screen, 0x300, 0x100, 1, 0x7dc0, 0, 0, 0, 0, 0x100, 0xf0, pic.backgroundLevel, SpriteBlend::Opaque); // tpage 0xbc
        }
        if (pic.page && !replaced) {
            const int pageX = pic.vramX & ~0x3f, pageY = pic.vramY & 0x100;
            if (pic.decor)
                drawSprite(screen, pageX, pageY, pic.mode, 0x7cc0, pic.x + pic.decorU, pic.y + pic.decorV, pic.decorU & 0xff,
                           pic.decorV & 0xff, pic.decorW, pic.decorH, pic.brightness, SpriteBlend::Subtract);
            // the PC's pages are 0xd0 lines at y 0x18, the disc's the whole screen
            const int lines = od::dataSource().isPs1() ? 0xf0 : 0xd0;
            drawSprite(screen, pageX, pageY, pic.mode, uint16_t(int(pic.clutY) << 6), pic.x, pic.y, 0, 0, 0x100, lines, pic.brightness,
                       SpriteBlend::Add);
        }
        const int W = kScreenW * kStaffScale, H = kScreenH * kStaffScale;
        staffPixels.resize(size_t(W) * H);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const Rgb5& c = screen[size_t(y / kStaffScale) * kScreenW + x / kStaffScale];
                const auto e = [](int v) { return uint32_t((v << 3) | (v >> 2)); };
                staffPixels[size_t(y) * W + x] = e(c.r) | e(c.g) << 8 | e(c.b) << 16 | 0xff000000u;
            }
        if (staffTex) r.updateTexture(staffTex, staffPixels.data());
        else staffTex = r.createTexture(W, H, staffPixels.data());
        staffShown = pic;
    }
    const float s = height / float(kScreenH);
    const ui::Vec2 a((width - kScreenW * s) * 0.5f, 0.0f), b(a.x + kScreenW * s, height);
    cv.quadFilled(ui::Vec2(0.0f, 0.0f), ui::Vec2(width, 0.0f), ui::Vec2(width, height), ui::Vec2(0.0f, height), ui::rgba(0, 0, 0, 255));
    cv.imageQuad(staffTex, a, ui::Vec2(b.x, a.y), b, ui::Vec2(a.x, b.y), ui::Vec2(0.0f, 0.0f), ui::Vec2(1.0f, 0.0f), ui::Vec2(1.0f, 1.0f),
                 ui::Vec2(0.0f, 1.0f), ui::rgba(255, 255, 255));
    if (!hdPage.empty()) { // the HD page, the window's height, centred (16:9: the whole width of a wide window)
        float aspect = 16.0f / 9.0f;
        if (const od::TextureHandle t = creditTexture(r, hdPage, aspect)) {
            const int v = std::min(255, int(pic.extra >= 0 ? pic.extraBrightness : pic.brightness) * 2);
            const float w = height * aspect, x0 = (width - w) * 0.5f;
            cv.imageQuad(t, ui::Vec2(x0, 0.0f), ui::Vec2(x0 + w, 0.0f), ui::Vec2(x0 + w, height), ui::Vec2(x0, height),
                         ui::Vec2(0.0f, 0.0f), ui::Vec2(1.0f, 0.0f), ui::Vec2(1.0f, 1.0f), ui::Vec2(0.0f, 1.0f),
                         ui::rgba(v, v, v, v));
        }
    }
}

void HudUi::draw(ui::Canvas& cv, const std::vector<od::hud::Prim>& prims, float width, float height) const {
    const float s = height / 240.0f;
    const ui::Vec2 o((width - 320.0f * s) * 0.5f, 0.0f);
    auto P = [&](float x, float y) { return ui::Vec2(o.x + x * s, o.y + y * s); };
    auto col = [](uint32_t c) { return ui::rgba(c & 0xff, (c >> 8) & 0xff, (c >> 16) & 0xff, 255); };
    for (const od::hud::Prim& p : prims) {
        if (p.kind == od::hud::Prim::Textured) {
            const auto it = tex.find(uint32_t(p.tpage) << 16 | p.clut);
            if (it == tex.end()) continue;
            auto UV = [&](int k) { // inset by an eighth of a texel: no bleeding from the neighbours in the atlas
                const float du = p.u[k] < (p.u[0] + p.u[1] + p.u[2] + p.u[3]) * 0.25f ? 0.125f : -0.125f;
                const float dv = p.v[k] < (p.v[0] + p.v[1] + p.v[2] + p.v[3]) * 0.25f ? 0.125f : -0.125f;
                const bool flatU = p.u[0] == p.u[1], flatV = p.v[0] == p.v[2];
                return ui::Vec2((p.u[k] + (flatU ? 0.5f : du)) / 256.0f, (p.v[k] + (flatV ? 0.5f : dv)) / 256.0f);
            };
            cv.imageQuad(it->second, P(p.x[0], p.y[0]), P(p.x[1], p.y[1]), P(p.x[3], p.y[3]),
                             P(p.x[2], p.y[2]), UV(0), UV(1), UV(3), UV(2),
                             p.semi ? ui::rgba(255, 255, 255, 128) : ui::rgba(255, 255, 255));
        } else if (p.kind == od::hud::Prim::Flat) {
            cv.quadFilled(P(p.x[0], p.y[0]), P(p.x[1], p.y[1]), P(p.x[3], p.y[3]), P(p.x[2], p.y[2]), col(p.colour));
        } else {
            const ui::Vec2 a = P(p.x[0], p.y[0] + 0.5f), b = P(p.x[1], p.y[1] + 0.5f); // through the pixel centres
            cv.line(ui::Vec2(a.x + 0.5f, a.y + 0.5f), ui::Vec2(b.x + 0.5f, b.y + 0.5f), col(p.colour), s);
        }
    }
}

} // namespace od::app
