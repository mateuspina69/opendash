#include "app/ui/hud_ui.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <exception>

#include "app/room_mesh.hpp"
#include "engine/dat.hpp"
#include "engine/data_source.hpp"
#include "engine/vram.hpp"

namespace od::app {

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
    if (!pic.background && !pic.page) return;
    const auto same = [](const od::port::StaffPicture& a, const od::port::StaffPicture& b) {
        return a.background == b.background && a.backgroundLevel == b.backgroundLevel && a.page == b.page &&
               a.brightness == b.brightness && a.vramX == b.vramX && a.clutY == b.clutY && a.segment == b.segment &&
               a.decor == b.decor;
    };
    if (!staffTex || !same(pic, staffShown)) {
        static std::array<Rgb5, kScreenW * kScreenH> screen;
        screen.fill({});
        // the OT: the background's list (0x88) before the page's (0x84); each list's last packet first
        if (pic.background) {
            drawSprite(screen, 0x380, 0x100, 1, 0x7dc0, 0x100, 0, 0, 0, 0x40, 0xf0, pic.backgroundLevel, SpriteBlend::Opaque); // tpage 0xbe
            drawSprite(screen, 0x300, 0x100, 1, 0x7dc0, 0, 0, 0, 0, 0x100, 0xf0, pic.backgroundLevel, SpriteBlend::Opaque); // tpage 0xbc
        }
        if (pic.page) {
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
