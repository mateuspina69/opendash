// The 2D primitives OpenDash's screen passes draw for the exe's packet builders (the title task's, the status
// sub-screen's): one PS1 / PC packet of the 320 x 240 (or 640 x 480) screen each, already in drawing order.
#pragma once

#include <cstdint>
#include <vector>

namespace od::port {

// A model preview's triangle (OpenDash): the posed model in view space (game units << the stage shift, x right, y
// down, z away from the viewer), PS1 texels (u, v of the 256 x 256 window of tpage / clut), the corner colours
// (0xBBGGRR, 0x80 = 1.0)
struct ModelTri {
    float p[3][3];
    uint8_t u[3], v[3];
    uint32_t colour[3];
    uint16_t tpage, clut;
};
struct ModelPreview {
    std::vector<ModelTri> tris;
};

// the vector UI's pictures of the containers (ScreenPrim::vec): the host draws them instead of the pixel art
enum VecArt : uint16_t {
    kVecNone = 0,
    kVecStatusPanel = 1, // the status sub-screen's tile map (FUN_005e7920)
    kVecFrame0 = 2,      // the 9-slice frames (FUN_005e8130) by style: 0 the ringed frame (descriptions),
    kVecFrame1 = 3,      // 1 the round list frame,
    kVecFrame2 = 4,      // 2 the frame with the arrow (drawn by code at the call's size: app/ui/vector_ui)
    kVecOptJunction = 5, // the options page's corner pieces where its two frames meet (FUN_005eeda0, 0x10, 0x94)
    kVecItemIcon = 0x100, // + k: an item's 16 x 16 icon (FUN_005ea6d0, kind k of 0x91aab0)
};

// what a primitive of the 2D screens shows, for the vector UI (ScreenPrim::label / labelArg): the host draws it as
// vector shapes / text / a button prompt instead of the pixel art (and draws the pixel art when it cannot)
enum VecLabel : uint16_t {
    kLblNone = 0,
    kLblStatusTitle,    // "STATUS" between its two rings (FUN_005e7920: 0x70, 0x10, 96 x 16)
    kLblStatusLocation, // the "location" caption (0x88, 0x20, 48 x 16)
    kLblStatusRow,      // a row of the status panel (0x91a998): arg = the row | 0x10 the cursor's row
    kLblGaugeCell,      // a cell of the life gauge (8 x 7): arg 0 = a capacity cell, 1 = life
    kLblWeaponSwap,     // the special weapon caption (0x68, 0x98, 64 x 16): arg 1 = dimmed (no special weapon)
    kLblWeaponLifter,   // its "<-> lifter" half (0xa8, 0x98, 48 x 16): arg 1 = dimmed
    kLblPrompt,         // a button glyph of the key configuration: arg = the PS1 bit's index | 0x100 dimmed
    kLblMenuBar,        // the sub-screen's "< confirm  cancel >" bar (0x68, 0xd0, 128 x 16)
    kLblPageTitle,      // a page's title between its rings (FUN_005e9b40): arg = the page table index k
    kLblPageRow,        // a row of a page's row table (FUN_005e9970): ref = the page << 16 | the caption's uv (its
                        // picture in the page's art: the same caption in any table), arg = the row | state << 8
                        // (0 other, 1 the cursor's, 2 chosen on a deeper level, 3 another on a deeper level)
    kLblPageTab,        // the L / R tabs at the screen's edges (FUN_005e7ee0): arg = 0 L / 1 R | the page << 8
    kLblOptValue,       // an option's value / label box (FUN_005eeda0, the tables gVal): ref = the caption's uv,
                        // arg = 0 another value, 1 the chosen value, 2 a label (BGM, SE, Buster, ...)
    kLblOptRail,        // the rail under an option row (FUN_005eeda0: the quad 0x4c .. 0x118 and its end caps)
    kLblNone2,          // drawn by another label (the rail's end caps): the vector UI skips it
    kLblFrameArrow,     // the green frame's middle arrow (FUN_005e8130 style 2: 16 x 16 on its right side)
    kLblVolBox,         // a volume bar's box (FUN_005eeda0 / FUN_005ef210, 64 x 16): arg 1 = being set
    kLblVolLevel,       // its level (the bars up to the sprite's width)
    kLblEquipHead,      // an equipment summary box's caption (FUN_005ed1a0, 0x91b534): arg = its variant (0 / 1
                        // buster, 2 / 3 body -- its caption leads with a crossed box --, 4 special weapon)
    kLblScrollCap,      // a list's scroll bar (FUN_005e9ca0) end piece, 8 x 16: arg 0 = the top one, 1 = the bottom one
    kLblScrollTrack,    // its track (the quad between the end pieces)
    kLblScrollThumb,    // its thumb (the quad over the track)
    kLblScrollLit,      // an end piece's arrow lit while the list scrolls (8 x 8): arg 0 = up, 1 = down
};

struct ScreenPrim {
    // Sprite  = SPRT (codes 0x64 / 0x74 8x8 / 0x7c 16x16; the PC's 0x8c): x y w h, u v, the clut, tpage = the draw
    //           mode in effect (the e1 word's low bits)
    // Flat    = TILE (0x60 / 0x62): x y w h, colour
    // Gouraud = shaded quad (0x38): corners qx / qy, colours qc (0 1 2 3 = TL TR BL BR)
    // Quad    = a 4-point polygon (0x28 flat / 0x2c textured): corners qx / qy, qu / qv, colour qc[0], tpage, clut
    // Glyph   = the PC's font packet (0x84, FUN_0040b610): glyph `code` of font slot `font` at (gx, gy), `size` texels
    //           (shown size / 2 px), sampled from `u` texels into its cell (FUN_0055ba50's proportional ASCII)
    // Text    = a string of a message bank (FUN_005578c0, whose glyphs are 0x84 packets): bank, id at x y, align
    //           (FUN_005578c0's mode & 0xf) from ax; lang = the bank is a language pack's copy; or (bank 0) an
    //           OpenDash string: utf8 (the Latin font) / pc (PC glyph codes, 0-terminated)
    // Clip    = the drawing area (0xe4, FUN_0040b650): x y w h
    // Line    = a flat line segment (0x40, one per segment of a 0x48 polyline): qx / qy [0] -> [1], colour (both end
    //           pixels drawn, as the GPU does)
    // Tri     = a shaded triangle (0x30 / 0x32 G3): corners qx / qy [0..2], colours qc [0..2]; semi = the code's bit,
    //           blended by the draw mode in effect (tpage bits 5..6: 0 = (B + F) / 2)
    enum Kind : uint8_t { Sprite, Flat, Gouraud, Quad, Glyph, Text, Clip, Line, Tri } kind = Sprite;
    int16_t x = 0, y = 0, w = 0, h = 0;
    uint8_t u = 0, v = 0;
    uint16_t tpage = 0, clut = 0; // tpage = the draw-mode word's low bits (page, blend mode bits 5..6, colour mode)
    uint32_t colour = 0x808080;   // 0xBBGGRR (textured: modulation, 0x80 = 1.0)
    bool semi = false;            // the code's semi-transparency bit (blend = tpage bits 5..6)
    bool raw = false;             // code bit 0: texels unmodulated
    bool half = false;            // the PC's 0x8c (FUN_00405990): w x h on screen from 2w x 2h texels (u + 2w)
    bool textured = false;        // Quad
    // Sprite whose texels are a copy of the frame buffer (a MoveImage of the picture into VRAM drawn back, e.g. the
    // development room's weapon picture 0x545ca0): the w x h rect at (grabX, grabY) of the 320 x 240 screen as the 3D
    // pass left it (the host's Renderer::sceneGrab), drawn at x y
    bool grab = false;
    int16_t grabX = 0, grabY = 0;
    // OpenDash (not the game's): Sprite whose picture is the 3D model preview `model` (game/model_preview modelPreview:
    // the special weapon's turning model of the equipment page / the development room), rendered by the host's GPU
    // into the w x h rect at x y
    int8_t model = -1;
    // OpenDash: nonzero = the primitive is part of a container picture (a panel's tile map, a 9-slice frame;
    // game/menu_draw artBegin): the primitives with the same id, consecutive in the list, form one picture the host
    // may redraw as vector shapes (app/ui/hd_art)
    uint16_t art = 0;
    // OpenDash: the vector picture that stands for that container (VecArt; 0 = none: the host fits the pixel art)
    uint16_t vec = 0;
    uint16_t label = 0, labelArg = 0; // OpenDash: VecLabel
    uint32_t labelRef = 0;            // OpenDash: the label's reference (kLblPageRow)
    int16_t row = -1; // OpenDash: the row of the cursor list (FUN_005e83d0) the primitive shows (the mouse's targets);
                      // 0x100 + row * 2 + column: a scrolling list's visible cell (FUN_005ea8a0 / FUN_005e9730)
    bool hitOnly = false; // OpenDash: only a mouse target (menu hitRow): not drawn
    int16_t qx[4]{}, qy[4]{};     // Gouraud / Quad corners
    uint32_t qc[4]{};             // Gouraud colours
    uint8_t qu[4]{}, qv[4]{};     // Quad texels
    uint16_t code = 0;            // Glyph
    uint8_t font = 0, size = 0, palette = 0;
    float gx = 0, gy = 0;         // Glyph
    uint32_t bank = 0;            // Text
    const char* utf8 = nullptr;
    const uint16_t* pc = nullptr;
    int16_t id = 0, ax = 0;
    uint8_t align = 0;
    bool ruby = true, lang = false;
};

} // namespace od::port
