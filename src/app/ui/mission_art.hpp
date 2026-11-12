// OpenDash: the scripted banner's "MISSION START" / "MISSION COMPLETE" (game/hud overlayDrawPass, kinds 1 and 2) drawn
// again in HD in the text's language: assets/ui/mission/<uiTextCode>.png (an atlas of the pieces, RGBA) and
// <code>.txt (a piece per line: kind, its atlas rect in pixels, its place and size in the banner's units, the radius
// and angle it flies in from). The pieces fly in like the game's sprites, on the banner's own clock (its sprites'
// radii); a language without the files keeps the game's sprites.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "render/renderer.hpp"
#include "render/ui_canvas.hpp"

namespace od::app {

struct MissionArt {
    // the banner shown this frame drawn in HD for language `code`; false = not a mission banner / no art (the caller
    // draws the game's sprites)
    bool draw(od::Renderer& r, ui::Canvas& cv, const std::string& code, float width, float height);

private:
    struct Piece {
        int kind = 0;
        float ax = 0, ay = 0, aw = 0, ah = 0; // atlas pixels
        float dx = 0, dy = 0, dw = 0, dh = 0; // banner units (the 320 x 240 screen's pixels)
        float radius = 0;
        int angle = 0;
    };
    struct Art {
        bool tried = false;
        std::vector<Piece> pieces;
        od::TextureHandle tex = 0;
        int w = 0, h = 0;
    };
    std::map<std::string, Art> arts_;
    Art* art(od::Renderer& r, const std::string& code);
};

} // namespace od::app
