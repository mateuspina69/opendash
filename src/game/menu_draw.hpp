// The 2D drawing of the menus (the status sub-screen, the title's load menu, the shop / development room / script
// lists): the exe's packet builders link their packets into the ordering table 0xa67840 at the entry they use, one
// chain per insertion; here the chains are collected and flushed into the caller's ScreenPrim list in the exe's walk
// order (the highest entry first, the chains of one entry newest first, a chain's packets in order; the draw mode, the
// PC's 0xe1 word, carries over from packet to packet). Also the shared pieces several menus build: the bank strings
// (exe 0x5578c0), the PC font's ASCII (exe 0x55ba50), the scrolling backdrop (exe 0x5e7fe0), a list's scroll bar
// (exe 0x547ff0) and OpenDash's widescreen margin and its tags of the primitives (the container pictures, the vector
// labels, the mouse's rows).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/screen_prim.hpp"

namespace od::port {

struct ListCursor;

// OpenDash widescreen: px past each side of the 320 x 240 frame (ceil((240 * aspect - 320) / 2), 0 at 4:3); the
// sub-screen's backdrop (its drawing area) continues there and the L / R tabs go to the edges
void setScreenExtra(int px);
int screenExtra();

// exe 0x5e7fe0(0) for a caller outside the sub-screen (the title's load menu): the backdrop into `draw` (nullptr: none)
void loadMenuBackdropDraw(std::vector<ScreenPrim>* draw);

namespace menu_draw {

// ---- the sink ----
// the primitives go into `draw` (nullptr: the chains are built and dropped; the state the builders write is written
// either way)
void begin(std::vector<ScreenPrim>* draw);
// the chains built so far dropped
void discard();
// the chains flushed into the sink in the walk order, the sink closed
void end();
// the caller asked for the primitives
bool drawing();

// ---- the ordering table's entries the menus use (walked from the highest) ----
constexpr uint32_t kOtModel = 0x70;    // + ot * 4: the sub-screen's 3D actors (game/status_weapon_model)
constexpr uint32_t kOtOverlay = 0x78;  // the overlays (the options' screens, the ASCII texts), the switching backdrop
constexpr uint32_t kOtText = 0x7c;     // the bank texts, the icons, the lists' drawing areas
constexpr uint32_t kOtRows = 0x80;     // the rows, the cursors, the panel's sprites
constexpr uint32_t kOtFrames = 0x84;   // the frames, the panel's tile map
constexpr uint32_t kOtBackdrop = 0x88; // the backdrop

// the PS1 packet codes the builders use (their low bits: 2 semi-transparent, 1 raw texels)
constexpr uint8_t kSpriteFree = 0x64; // SPRT w x h
constexpr uint8_t kSprite8 = 0x74;    // SPRT 8 x 8
constexpr uint8_t kSprite16 = 0x7c;   // SPRT 16 x 16
constexpr uint8_t kSpriteHalf = 0x8c; // the PC's half-size sprite (exe 0x405990): w x h from 2w x 2h texels
constexpr uint8_t kTileOpaque = 0x60, kTileSemi = 0x62;
constexpr uint8_t kPolyF3 = 0x20, kPolyF4 = 0x28, kPolyFT4 = 0x2c;
constexpr uint32_t kNeutral = 0x808080; // a textured packet's colour: the texels as they are
constexpr uint32_t kDim = 0x505050;     // the dimmed rows / texts
// a sprite's texture word: clut << 16 | v << 8 | u
constexpr uint32_t spriteUv(uint32_t clut, uint32_t u, uint32_t v) { return clut << 16 | v << 8 | u; }

// ---- the chains ----
// a new chain in the entry `slot`; returns its index
size_t otOpen(uint32_t slot);
// a primitive of kind k appended to the chain c (tagged with the pending art / label / row)
ScreenPrim& otPut(size_t c, ScreenPrim::Kind k);
// the chain c's last primitive
ScreenPrim& otLast(size_t c);
// the draw mode (the 0xe1 word's low bits) from here on in the walk
void otDrawMode(size_t c, uint16_t word);
// SPRT: `uv` = clut << 16 | v << 8 | u (spriteUv)
void otSprite(size_t c, int x, int y, uint32_t uv, int w, int h, uint32_t col = kNeutral, uint8_t code = kSpriteFree);
// TILE (kTileOpaque / kTileSemi)
void otTile(size_t c, int x, int y, int w, int h, uint32_t col, uint8_t code);
// FT4 / F4 / F3: corners 0 1 2 3 = TL TR BL BR
struct Q4 {
    int a[4];
};
void otQuad(size_t c, Q4 x, Q4 y, uint32_t col, uint8_t code, Q4 uu = {}, Q4 vv = {}, uint16_t clut = 0,
            uint16_t tpage = 0);
// the PC's font packet 0x84 (exe 0x40b610 and the fields its callers write)
void otGlyph(size_t c, int x, int y, uint16_t code, uint32_t col = kNeutral, uint8_t font = 0, uint8_t size = 0x18);
// the drawing area (0xe4, exe 0x40b650 -> 0x4070e0: the y of the second buffer - 0x100)
void otClip(size_t c, int x, int y, int w, int h);
// ... the drawing area of the buffer the frame draws into
void otClipDrawArea(size_t c);
// a monochrome line (0x48 LINE_F) the exe draws axis-aligned: a one-pixel TILE with both ends
void otHLine(size_t c, int x0, int x1, int y);
void otVLine(size_t c, int x, int y0, int y1);
// OpenDash: a mouse target only (ScreenPrim::hitOnly, not drawn) over the w x h rect at x y for row `row`
void otHitRow(size_t c, int x, int y, int w, int h, int row);

// ---- OpenDash's tags of the next primitives ----
// the container picture the primitives belong to (ScreenPrim::art; vec: its vector stand-in, VecArt): artBegin()
// before a panel's tile map / a frame's pieces, artEnd() after them (the icons drawn with them stay outside)
void artBegin(uint16_t vec = 0);
void artEnd();
// the VecLabel of the next primitive (only that one)
void labelNext(uint16_t label, uint16_t arg = 0, uint32_t ref = 0);
// the cursor list row (ScreenPrim::row) of the next primitive
void rowNext(int row);

// ---- the shared builders ----
// exe 0x5578c0(packed, bank, id, mode): a bank string as one Text primitive in kOtText (the host lays the glyphs out):
// packed = textPlace(x, y, palette, dim); mode & 0xf the alignment over 10 cells of 12 px (kTextSpread / kTextRight /
// kTextCentred), kTextNoRuby
void bankTextDraw(uint32_t packed, uint32_t bank, int16_t id, uint32_t mode);
constexpr uint32_t kTextSpread = 1, kTextRight = 2, kTextCentred = 3, kTextNoRuby = 0x10;
constexpr uint32_t kTextDimBit = 0x400000; // packed: brightness 0x50 instead of 0x80
// the place argument of bankTextDraw: x (10 bits) | y << 10 (9 bits) | palette << 19 (3 bits; its bit 3 = dim)
constexpr uint32_t textPlace(uint32_t x, uint32_t y, uint32_t palette = 0) {
    return ((y & 0x1ff) | palette << 9) << 10 | (x & 0x3ff);
}
constexpr uint32_t kTextPaletteDim = 8; // a palette value that dims (the packed dim bit)
// the items page's count text "nn/nn" (exe .data; game/status_items writes its digits, PS1-encoded under a pack)
// exe 0x55ba50(text, x, y, proportional): an ASCII text as 0x84 glyphs of font slot 0 in kOtOverlay (12 px cells, or
// the widths of the exe's table, each glyph sampled from its left offset)
void asciiTextDraw(const std::string& text, int x, int y, bool proportional);
// exe 0x55ba10(text): its proportional width (the table's widths; a space 4) / 2
uint32_t asciiTextWidth(const std::string& text);
// exe 0x5e7fe0(switching): the scrolling backdrop, 21 x 16 tiles of 16 px (tpage 0xd, clut 0x7f92) in kOtBackdrop
// (kOtOverlay while a page switch runs). OpenDash (widescreen): the drawing area opened screenExtra() px past each side
// of the 320 frame and the tile columns continued there (same phase); 4:3 = the exe's 21 columns
void backdropDraw(int switching);
// exe 0x547ff0(list, x, y) (the script lists' copy exe 0x53e5d0): a list's scroll bar in kOtText (tpage 0xd, clut
// 0x7f53): the top cap, the rail (an FT4 of the one-texel row v 0x90), the bottom cap, the arrow of a running move
// (up / down), the thumb
void listScrollBarDraw(const ListCursor& list, int x, int y);
// the pulse of the red cursor bars (the frame counter)
uint32_t barPulse();

} // namespace menu_draw

} // namespace od::port
