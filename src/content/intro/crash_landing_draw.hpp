// The crash-landing storm of script 6 (ST3A) as the game draws it (presentation only: reads the game state, never
// writes it).
//
// code address that writes a GPU packet / draw command. Besides the room geometry, the models, the sky gradient
// (exe 0x5f03a0, already drawn by the app) and the opening effect class's draw commands (content/intro/
// opening_effects_draw), the storm is:
//  * the script's 2D backdrop, exe 0x517cf0 (the script's backdrop bytes): an 8 x 8 grid of 128-pixel sprites (tpage
//    0x1b, clut 0x7c80: the clouds seen from above) linked at the head of OT slot 0x88, scrolled by its speeds;
//  * the ST3A static type 0x12, the cloud rings exe 0x598b00, whose variants draw while they update:
//      variant 2 exe 0x599580: 4 rings x 32 camera-facing 64x64 cloud puffs (billboard-style quads in the depth OT)
//                turning around the static, one CLUT per ring;
//      variant 3 exe 0x599a50 / variant 5 exe 0x59a3e0: a 2D panorama -- rows of 16x16 sky-gradient tiles, a band of
//                32-pixel cloud columns that scrolls with the camera yaw (the ring's angle 1) and, variant 3, a sea
//                band (angle 2) linked at the static's depth (it covers the puffs behind the static: the Forbidden
//                Island landing's whirlwind, ST49, sinks into it);
//  * the opening effect's flash ring (variant 11, exe 0x436f00): an additive orange glow on the screen (64 G3
//    triangles, bright centre fading to black over an ellipse growing 0xb0 + t * 32 for 8 ticks), OT slot 0x78;
//  * the white flashes are screen fades (fade types 9..0xc / 0x19..0x1c: additive, od::port::fadeAdditive);
//  * draw commands (exe 0x5f0580: billboards 0x2c, gouraud rings 0xf0) of the crash's other classes, rebuilt word for
//    word from the post-tick state and decoded like the draw pass: the falling piece exe 0x5cbf00 (its diving variant
//    exe 0x5cbf40: a glow disc on its 2nd / 3rd tick; both variants a fire trail of 16 additive billboards, exe
//    0x5cc150), the flash exe 0x431120 in its glow state (exe 0x431750: three rings), the enemy shot exe 0x5c52f0
//    (spawn tick: a glow disc, exe 0x5c6fc0; then one billboard per tick, exe 0x5c5440) and the landing crash craft's
//    exhaust glows (exe 0x4bb750 variant exe 0x4bb9b0, exe 0x4bbec0: two discs).
// No rain / lightning primitive exists: nothing else writes packets during the script. Not drawn here:
// the blast exe 0x563b10 (0xf8 fans) and the opening effect class's variants 4..10 (content/intro/opening_effects_draw).
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "game/effects_draw.hpp"

namespace od::storm {

// A textured sprite in the PS1 frame (320 x 240, packet coordinates), colour 0x808080 (neutral), opaque.
struct Sprite {
    int16_t x = 0, y = 0;
    uint16_t w = 0, h = 0;
    uint8_t u = 0, v = 0;
    uint16_t tpage = 0, clut = 0; // the texture (od::fx::Texture key, see textures())
    int texture = -1;             // index into the fx::TextureCache passed to collect(), -1: none
    bool original = true;         // false: added left / right of the 320-pixel frame for wider windows
};

// A gouraud triangle in the PS1 frame, colours 0xBBGGRR, drawn additively (PS1 semi mode 1).
struct Tri {
    float x[3]{}, y[3]{};
    uint32_t colour[3]{};
    int row = -1;       // the gradient row (od::fx::gradients() + gradients()) of the non-black colour, -1 none
    float shade = 1.0f; // its brightness on that row
};

// Sprites linked into the depth OT at a static's projected depth (exe 0x5f2f20's view z >> 2): the polygons farther
// than the static are drawn before them (hidden where they cover), the nearer ones after. Off the effects' range the
// exe's depth stays 0: OT slot 0, whose link the frame's splice of the depth OT overwrites (exe 0x40b5d0 with the
// depth OT's entry 0 as the chain's end, e.g. 0x542358): never drawn (the attract intro's window scene, ST02 sub 1).
struct DepthChain {
    float centre[3] = {0, 0, 0}; // the static's position (game units, game axes: Y down)
    int16_t slot = 0;            // the OT slot (view z >> 2); 0: off range, dropped by the splice
    std::vector<Sprite> sprites;
};

struct Frame {
    std::vector<Sprite> back;       // behind the 3D scene, in drawing order (the OT slot 0x88 chains)
    std::vector<DepthChain> depth;  // in the depth OT (the sea bands of exe 0x599a50)
    std::vector<Tri> front;         // over the 3D scene (OT slot 0x78), additive
    void clear() {
        back.clear();
        depth.clear();
        front.clear();
    }
};

// One draw-command record as the exe writes it at the draw command cursor (game/draw_commands) (w[0] = the record's first word; a 0x2c
// billboard's w[0] is left unwritten by the emitters: 0 here), with the code address that writes its word 1.
struct DrawCommand {
    uint32_t pc = 0;
    std::vector<uint32_t> w;
};

// The gradient rows of the storm's gouraud rings (colour at u 0 -> black), appended after od::fx::gradients(): a
// ring of another brightness of the same hue uses the row with a shade.
std::vector<std::array<uint32_t, 2>> gradients();

// The texture pages the storm samples in stage `stage` (ST3A: the backdrop page and the static entries 3 / 7 of the
// table 0x8ce350, with the ring CLUTs of variant 2), registered up front in the app's fx::TextureCache when it builds
// an ST3A room (the tables: the bound data source's). Empty for other stages (collect() registers what it meets).
std::vector<fx::Texture> textures(int stage);

// Every storm element of the post-tick state (the game state, any stage): the 2D sprites / triangles into `frame`, the
// 3D cloud puffs into `prims` (od::fx::Prim; Sprite::texture / Prim::texture = indices of `tex`). `rowBase` = the
// number of od::fx::gradients() rows before gradients(); `commands` (tests): the rebuilt draw commands.
void collect(fx::TextureCache& tex, int rowBase, Frame& frame, std::vector<fx::Prim>& prims,
             std::vector<DrawCommand>* commands = nullptr);

} // namespace od::storm
