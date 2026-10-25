// The frame-buffer copies of the exe's screen builders on the host: a ScreenPrim sprite with `grab` set shows a rect
// of this frame's 3D picture (the PS1 MoveImage of a frame-buffer rect into VRAM, then a sprite of that VRAM). The
// development room's weapon picture (FUN_00545ca0 / Nino's FUN_00548490, content/yosyonke/dev_room devStatsDraw): the 48 x
// 48 rect around the screen centre, where the field pass drew the turning model, shown in the stats panel. The 2D
// screens are painted on the 320 x 240 screen scaled by s at offset o (PrimPainter), the 3D picture fills the window
// with the PS1 screen centre at the window centre: a PS1 screen point (X, Y) is the window pixel o + (X, Y) * s in
// both.
#pragma once

#include "render/ui_canvas.hpp"

#include <cmath>

#include "engine/screen_prim.hpp"
#include "render/renderer.hpp"

namespace od::app {

inline void paintFrameGrab(od::Renderer& r, ui::Canvas& cv, const od::port::ScreenPrim& p, float s, ui::Vec2 o) {
    if (p.w <= 0 || p.h <= 0) return;
    auto px = [](float v) { return int(std::floor(v + 0.5f)); };
    const int sx0 = px(o.x + float(p.grabX) * s), sy0 = px(o.y + float(p.grabY) * s);
    const int sx1 = px(o.x + float(p.grabX + p.w) * s), sy1 = px(o.y + float(p.grabY + p.h) * s);
    const od::TextureHandle t = r.sceneGrab(sx0, sy0, sx1 - sx0, sy1 - sy0);
    if (!t) return;
    // the sprite's colour modulates (0x80 = 1.0; FUN_00545ca0 uses 0x808080)
    const uint32_t c = p.colour;
    auto ch = [&](uint32_t v) { return p.raw ? 255u : (v * 2 > 255 ? 255u : v * 2); };
    const ui::Color col = ui::rgba(ch(c & 0xff), ch((c >> 8) & 0xff), ch((c >> 16) & 0xff), p.semi ? 128 : 255);
    const ui::Vec2 a(float(px(o.x + float(p.x) * s)), float(px(o.y + float(p.y) * s)));
    const ui::Vec2 b(float(px(o.x + float(p.x + p.w) * s)), float(px(o.y + float(p.y + p.h) * s)));
    cv.image(t, a, b, ui::Vec2(0.0f, 0.0f), ui::Vec2(1.0f, 1.0f), col);
}

} // namespace od::app
