// HD containers (OpenDash, F1 "paineis HD" / --hd-art 0 = the pixel art): a container picture of the 2D screens (the
// primitives with the same ScreenPrim::art id: a panel's tile map, a 9-slice frame) composed at 1x from the VRAM and
// redrawn as clean vector shapes fitted to it. Each panel of the picture (a connected opaque region) is measured: the
// colour bands along each side (the outer bands the four sides share: border, bevel; widths per side), the inside
// colour and each corner's shape (a 45-degree chamfer or a round, its size). It is drawn as nested antialiased
// shapes -- the outline inset by the bands' widths, the corners shrunk concentrically -- in the pixel art's colours.
// What the shapes do not explain (the pegs, the icons fixed to a panel, a notch: pixels whose colour the shapes do not
// have within 1 px) is drawn on top as crisp pixels; small regions (icons alone) stay pixels.
#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/screen_prim.hpp"
#include "engine/vram.hpp"
#include "render/ui_canvas.hpp"

namespace od::app {

struct HdArt {
    bool enabled = true;

    // in the picture's pixels (origin = its top-left on the 320 x 240 screen)
    struct Shape {
        std::vector<ui::Vec2> pts; // convex outline
        ui::Color c;
    };
    struct Rect { // a run of crisp pixels
        int x, y, w, h;
        ui::Color c;
    };
    struct Shapes {
        int x = 0, y = 0;
        std::vector<Shape> shapes; // in drawing order
        std::vector<Rect> pixels;  // over the shapes
    };

    // draws prims[0..n) (one picture) at scale s / origin o; false = not a picture it can redraw (the caller draws
    // the primitives as they are)
    bool draw(ui::Canvas& cv, const od::Vram& vram, const od::port::ScreenPrim* prims, size_t n, float s, ui::Vec2 o);
    // the VRAM changed (PrimPainter::invalidate): decoded pages and shapes are dropped
    void reset();

    // the fit of a composed picture (w x h, 0 = transparent)
    static void fit(const std::vector<uint32_t>& px, int w, int h, Shapes& out);

private:
    std::map<uint32_t, std::vector<uint8_t>> pages_; // tpage << 16 | clut -> 256 x 256 RGBA
    std::unordered_map<uint64_t, Shapes> shapes_;     // the composed picture's hash
    std::set<std::string> dumped_;
    void dumpPicture(const std::vector<uint32_t>& px, int w, int h, int x, int y);
    const std::vector<uint8_t>& page(const od::Vram& vram, uint16_t tpage, uint16_t clut);
    bool compose(const od::Vram& vram, const od::port::ScreenPrim* prims, size_t n, int& bx, int& by, int& w, int& h,
                 std::vector<uint32_t>& px);
};

} // namespace od::app
