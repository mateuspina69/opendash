#include "app/ui/hud_ui.hpp"

#include <SDL3/SDL.h>

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
