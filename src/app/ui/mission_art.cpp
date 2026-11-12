#include "app/ui/mission_art.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <sstream>

#include <stb_image.h>

#include "app/ui/vector_ui.hpp"
#include "engine/dat.hpp"
#include "game/hud.hpp"

namespace od::app {

namespace {
constexpr float kRadiusStep = 12.0f;      // the fly-in's radius per frame (overlayDrawPass: -0xc)
constexpr float kTurn = 4096.0f;          // a full turn in angle units
constexpr float kPi = 3.14159265358979f;
} // namespace

MissionArt::Art* MissionArt::art(od::Renderer& r, const std::string& code) {
    Art& a = arts_[code];
    if (!a.tried) {
        a.tried = true;
        const auto dir = assetsDir() / "ui" / "mission";
        std::ifstream f(dir / (code + ".txt"));
        std::vector<Piece> pieces;
        for (std::string line; f && std::getline(f, line);) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream in(line);
            Piece p;
            if (in >> p.kind >> p.ax >> p.ay >> p.aw >> p.ah >> p.dx >> p.dy >> p.dw >> p.dh >> p.radius >> p.angle)
                pieces.push_back(p);
        }
        if (!pieces.empty()) {
            try {
                const std::vector<uint8_t> bytes = od::readFile(dir / (code + ".png"));
                int w = 0, h = 0, comp = 0;
                if (stbi_uc* px = stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h, &comp, 4)) {
                    a.tex = r.createTexture(w, h, reinterpret_cast<const uint32_t*>(px));
                    a.w = w;
                    a.h = h;
                    stbi_image_free(px);
                }
            } catch (const std::exception&) {
            }
        }
        if (a.tex) a.pieces = std::move(pieces);
        else SDL_Log("mission art: none for %s (the game's sprites stay)", code.c_str());
    }
    return a.tex ? &a : nullptr;
}

bool MissionArt::draw(od::Renderer& r, ui::Canvas& cv, const std::string& code, float width, float height) {
    using od::port::HudBannerStyle;
    od::port::HudBanner& b = od::port::hudBanner();
    if (b.shown == 0) return false;
    const HudBannerStyle style = HudBannerStyle(b.kind);
    if (style != HudBannerStyle::MissionStart && style != HudBannerStyle::MissionComplete) return false;
    Art* a = art(r, code);
    if (!a) return false;
    const int kind = int(b.kind);

    // the banner's clock: the game's sprite that flies in from the farthest (its radius goes down 12 a frame)
    float exeMax = 0.0f, elapsed = 0.0f, exeW = 0.0f;
    const uint32_t layout = b.layout.value();
    for (int i = 0; i < b.count; ++i) {
        if (!od::port::dataMapped(layout + uint32_t(i) * od::port::kBannerLayoutSpriteSize, od::port::kBannerLayoutSpriteSize))
            break;
        const od::port::HudBannerLayoutSprite t = od::port::bannerLayoutSprite(layout, uint32_t(i));
        exeW = std::max(exeW, float(t.x + t.w));
        if (float(t.radius) >= exeMax) {
            exeMax = float(t.radius);
            elapsed = (float(t.radius) - float(int16_t(b.sprites[size_t(i)].radius))) / kRadiusStep;
        }
    }
    float ourMax = 0.0f, ourW = 0.0f;
    for (const Piece& p : a->pieces)
        if (p.kind == kind) {
            ourMax = std::max(ourMax, p.radius);
            ourW = std::max(ourW, p.dx + p.dw);
        }
    if (ourMax <= 0.0f) return false;
    // our pieces land by the time the game's last sprite does
    const float radiusScale = exeMax > 0.0f && ourMax > exeMax ? exeMax / ourMax : 1.0f;
    const float centre = (exeW - ourW) * 0.5f; // the banner centred where the game's is

    const float s = height / 240.0f;
    const ui::Vec2 o((width - 320.0f * s) * 0.5f, 0.0f);
    const float tw = float(a->w), th = float(a->h);
    for (const Piece& p : a->pieces) {
        if (p.kind != kind) continue;
        const float rad = std::max(0.0f, p.radius * radiusScale - kRadiusStep * elapsed);
        const float ang = float(p.angle) * 2.0f * kPi / kTurn;
        // overlayDrawPass: x = (sin * r) >> 11, y = (cos * r) >> 11 with the 4.12 sine (2 r sin, 2 r cos)
        const float x = float(int16_t(b.x)) + centre + p.dx + 2.0f * rad * std::sin(ang);
        const float y = float(int16_t(b.y)) + p.dy + 2.0f * rad * std::cos(ang);
        const ui::Vec2 p0(o.x + x * s, o.y + y * s), p1(o.x + (x + p.dw) * s, o.y + (y + p.dh) * s);
        cv.image(a->tex, p0, p1, ui::Vec2(p.ax / tw, p.ay / th), ui::Vec2((p.ax + p.aw) / tw, (p.ay + p.ah) / th),
                 rad > 0.0f ? ui::rgba(255, 255, 255, 128) : ui::rgba(255, 255, 255));
    }
    return true;
}

} // namespace od::app
