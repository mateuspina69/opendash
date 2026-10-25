// The 2D packets of the exe's screen builders (the sub-screens, the message window frames) drawn on the game canvas.
#pragma once

#include "render/ui_canvas.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

#include "app/menu_vram.hpp"
#include "app/ui/hd_art.hpp"
#include "app/ui/vector_ui.hpp"
#include "engine/screen_prim.hpp"
#include "engine/vram.hpp"
#include "render/renderer.hpp"

namespace od::app {

// The untextured GPU primitives of the 2D builders (the message window frames, game/window_frame windowFrameDraw; the
// menus' lines and shaded triangles) on the 320 x 240 screen at scale s / origin o. A line covers the pixels the GPU
// draws (both ends; Bresenham, one rect per pixel), a G3 its corners' colours interpolated; semi-transparency = blend
// mode 0 ((B + F) / 2: 50 % alpha), the only mode these builders use.
struct FlatPaint {
    static ui::Color rgb(uint32_t c, int a = 255);
    static void line(ui::Canvas& cv, float s, ui::Vec2 o, int x0, int y0, int x1, int y1, ui::Color col);
    static void tri(ui::Canvas& cv, float s, ui::Vec2 o, const od::port::ScreenPrim& p);
    // the kinds without a texture or a font; false = not one of them
    static bool draw(ui::Canvas& cv, float s, ui::Vec2 o, const od::port::ScreenPrim& p);
    // all of them; vector (the vector UI): a frame's rounded corners -- the window frames' rings (lines joined end to
    // start) step 1 px further out at each ring with their corner's 45-degree cut growing by 1 px (3, 4, 5 px), so
    // together they draw a rounded corner: each cut becomes the quarter circle tangent at its ends (concentric arcs,
    // radii 1 px apart) and the ring a band 1 px wide through the pixel centres; the fill fans (G3s round a centre)
    // get the same arcs at their own cuts
    static void drawAll(ui::Canvas& cv, float s, ui::Vec2 o, const std::vector<od::port::ScreenPrim>& prims, bool vector);
    // the run starting at prims[i] (a ring of lines, a fan, else the one primitive): the index after it
    static size_t drawRun(ui::Canvas& cv, float s, ui::Vec2 o, const std::vector<od::port::ScreenPrim>& prims, size_t i,
                          bool vector);
    // a closed axis-aligned polygon's 45-degree cuts as quarter circles tangent at the cut's ends
    static std::vector<ui::Vec2> roundCuts(const std::vector<ui::Vec2>& pts);
    // the band of a polyline (screen units) 1 px wide, mitred joins, open ends cut half a pixel past the end
    static void band(ui::Canvas& cv, float s, ui::Vec2 o, const std::vector<ui::Vec2>& pts, bool closed, ui::Color col);
};

// The 2D packets of the exe's screen builders (engine/screen_prim.hpp: the status sub-screen) on the 320 x 240 screen,
// sampling an emulated VRAM (a page per tpage / clut, 4x nearest-upscaled like HudUi; the pages a VRAM write touched
// are decoded again). PS1 semantics: textured colours modulated (0x80 = 1.0) unless raw; semi-transparency drawn as
// 50 % alpha (blend mode 0 exactly; the menus use no other mode); the drawing area (0xe4) clips what follows.
struct PrimPainter {
    std::map<uint32_t, od::TextureHandle> tex;
    std::vector<od::TextureHandle> stale;
    HdArt art;        // the container pictures (ScreenPrim::art) fitted as shapes (until each has a vector picture)
    VectorUi* vector = nullptr; // the vector UI (the App's): pictures (ScreenPrim::vec), labels (ScreenPrim::label)
    static bool overlap(const std::array<int, 4>& r, int x, int y, int w, int h);
    void invalidate(std::vector<std::array<int, 4>>& dirty);
    od::TextureHandle page(od::Renderer& r, const od::Vram& vram, uint16_t tpage, uint16_t clut);
    // prims in drawing order; `text` draws the Glyph / Text kinds (the message fonts)
    void draw(od::Renderer& r, MenuVram& mv, ui::Canvas& cv, const std::vector<od::port::ScreenPrim>& prims, float s,
              ui::Vec2 o, const std::function<void(const od::port::ScreenPrim&)>& text, float sw = 320.0f, float sh = 240.0f,
              float ext = 0.0f);
};

} // namespace od::app
