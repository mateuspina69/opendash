// The message windows and the sub-screens' text drawn on the game canvas (the PC font, the language packs' PS1 fonts).
#pragma once

#include "engine/lang_pack.hpp"
#include "render/ui_canvas.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "engine/screen_prim.hpp"
#include "game/message_state.hpp"
#include "render/renderer.hpp"
#include "render/ui_font.hpp"

namespace od::app {

// Message windows on screen. The PC font is INIT.DAT's type-7 segments (Maybe_RegisterFontBitmap): slot 0 =
// 24x24 glyphs (drawn 12 px on the 320x240 screen), slot 1 = 48x48 (double size, 24 px), 4 bits per pixel
// (low nibble first), glyph g at g * (size / 2) * size (FUN_00406430). The glyph index comes from the script
// as FUN_00558110 reads it, so no character table is needed. Window layout (FUN_00557390 / the layout pass):
// frame rect +0x54 (s16 x, y, w, h), text origin +8 / +0xa, 12 px per glyph, 16 px per line, typed text up to
// the layout read pointer +0x34 from the page start +0x2c (u16 page header first), choice cursor +0x40 / +0x42
// relative to the origin. The frames are the exe's (game/window_frame windowFrameDraw: FUN_005583f0's six styles); the
// PC glyph palettes 0x8b176c are not modelled: the exe's glyphs are white with the 4-bit level as alpha.
struct MessageUi {
    // the system string `id` (item names, id - 0x380) of the current name bank (game/message setNameBank)
    static uint32_t nameAt(uint32_t id);
    struct Font {
        od::TextureHandle tex = 0;
        int cell = 0, cols = 0, count = 0, texW = 0, texH = 0;
    } font[2];
    bool tried = false;
    // PS1 Latin font of a language pack (engine/lang): bit plane 0 of the 4bpp page, 12x12 cells, 20 per row, glyph =
    // script byte; sprite width = advance = width table (SLUS_011.40 0x5c10c) below `limit`, else 12. Uploaded 4x
    // nearest-upscaled (sharp under the linear sampler).
    struct Ps1Font {
        od::TextureHandle tex = 0;
        std::array<uint8_t, 256> widths{};
        int limit = 0x6b; // glyphs below it take their width from the table, the rest 12 (widths.bin byte 256)
        // the button glyphs 0x51..0x54 drawn through their own CLUTs (buttons.bin, SLUS 0x80049ad0 / 0x8006b988): their
        // texels carry the CLUT colours (the whole nibble), the text colour only modulates their brightness
        bool buttons = false;
        static bool button(int g) { return g >= 0x51 && g <= 0x54; }
        // the text colours 0..6 (colours.bin: the PS1 glyph routine's CLUTs by window colour +0x21, gp + 0x2ec; planes
        // 0-1: stroke + shading); tex = colour 0
        std::array<od::TextureHandle, 8> col{};
        // the same colours for the HD text (MessageUi::hdGlyph): CLUT entry 1 = the glyph, 3 = its dark edge
        std::array<ui::Color, 8> fill{}, edge{};
        // the horizontal centre of glyph g's ink in its cell (PS1 pixels; < 0 = no ink): a lone HD glyph (the bullet)
        // is centred on it
        std::array<float, 256> inkCentre{};
        // the PS1 glyph routine (SLUS 0x800498c8): the sprite is `width` px wide and the text pass advances x by it
        float advance(uint8_t c) const { return c < limit ? float(widths[c]) : 12.0f; }
    };
    Ps1Font ps1;         // the language's pack
    Ps1Font labelFont;   // OpenDash's Latin labels (the language row): the PT-BR pack's (EN's glyphs + the accents)
    float ps1Advance(uint8_t c) const;
    std::string ps1Pack; // the pack ps1 was built from (LangPack::id)
    bool labelTried = false;
    static Ps1Font loadFont(od::Renderer& r, const od::lang::LangPack& pack, bool allColours = true);
    bool usePs1 = false; // draw with the pack font (set per frame from the language)
    // the language's pack font (Japanese: the PC font) and the labels' font
    // the language's pack (nullptr: Japanese) and the pack OpenDash's Latin labels are drawn with (nullptr: none)
    void usePack(od::Renderer& r, const od::lang::LangPack* pack, const od::lang::LangPack* labels);

    void load(od::Renderer& r); // INIT.DAT's fonts (the data source's)

    // HD text (OpenDash, default on; F1 / --hd-text 0 = the PS1 bitmaps): the Latin glyphs drawn from a TrueType font
    // (assets/fonts, a rounded monoline sans like the PS1's) at the window's resolution, in the PS1 glyphs' colours: cap
    // height 10 px of the 12 px cell, baseline at the cell's bottom, the CLUT's glyph colour over its dark edge. A run
    // of glyphs (a line, up to a cursor move) starts where the PS1 run starts and is spaced by the font's own advances
    // and kerning; a run wider than the PS1 run is tightened (its gaps, never its glyphs) to the PS1 width, so the
    // text stays inside the windows the game laid out. Icons (buttons, the hand, the JP marks) and glyphs the font
    // lacks stay the PS1 bitmaps, at their PS1 advance.
    bool hdText = true;
    // the window text's HD glyphs this much higher (PS1 px): the frame (FUN_005583f0) runs from the text's y - 3 to
    // y + 16 * lines + 4 and its cells start at y + 3, which centres the Japanese font (rows 1 .. 11 of the 12 px cell);
    // the Latin capitals sit on rows 2 .. 11, 8 px under the frame's top and 5 over its bottom -- raised 1.5 px the
    // margins above the capitals and under the baselines match
    static constexpr float kWindowTextRaise = 1.5f;
    mutable float hdRaise = 0.0f;
    bool vectorLines = false; // the vector UI: the frames' outlines as straight bands (FlatPaint::drawAll)
    mutable od::ui::Font hd; // a glyph cache (rasterized on demand while drawing)
    od::TextureHandle hdTex = 0;
    bool hdTried = false;
    void loadHd(od::Renderer& r);
    // uploads the glyphs rasterized while this frame's canvas was built (before the canvas is drawn)
    void flushHd(od::Renderer& r);
    // the TrueType code point of PS1 glyph g (0 = drawn from the bitmap)
    static uint32_t hdCodepoint(int g);
    // the code point PS1 glyph g is laid out with in the HD text (0x20 for the spaces; 0 = the PS1 bitmap)
    uint32_t hdLayoutCodepoint(int g, const Ps1Font& F) const;
    int hdPx(float s) const; // the HD font's em size (screen px) at scale s
    // the HD glyph cp, pen at screen x `pen` on the screen baseline `base`, in the colours of text colour `colour`
    void hdDraw(ui::Canvas& cv, uint32_t cp, float pen, float base, int px, float s, ui::Color col, const Ps1Font& F,
                int colour) const;
    // PS1 glyph g's bitmap at (x, y) (see glyphPs1)
    void ps1Bitmap(ui::Canvas& cv, int g, float x, float y, float s, ui::Vec2 o, ui::Color col, const Ps1Font& F,
                   int colour) const;
    struct RunGlyph {
        uint8_t g = 0;
        uint8_t colour = 0; // the text colour (CLUT) index
        ui::Color col = 0;
    };
    // a run of PS1 glyphs from (x, y) (PS1 px): all of them laid out, the first `shown` drawn (cv null: none); the
    // run's width (PS1 px). HD text: see hdText; else each at its PS1 advance
    float run(ui::Canvas* cv, const std::vector<RunGlyph>& gs, size_t shown, float x, float y, float s, ui::Vec2 o,
              const Ps1Font& F) const;
    // glyph g of font f at (x, y) on the 320x240 screen, `size` screen pixels
    void glyph(ui::Canvas& cv, int f, int g, float x, float y, float size, float s, ui::Vec2 o, ui::Color col) const;
    // PS1 glyph g at (x, y), 12x12 on the 320x240 screen; SLUS 0x80049b40: g j , . 1 px lower, p q y 2 px
    void glyphPs1(ui::Canvas& cv, int g, float x, float y, float s, ui::Vec2 o, ui::Color col, const Ps1Font* font = nullptr,
                  int colour = 0) const;
    // fb 45 / fb 21 / fb 23 / fb 44 replayed while a page is walked again (the slot list of the save / load menus: one
    // fb 45 per row); the inserted strings live in `loc` at the fake address kLoc
    static constexpr uint32_t kLoc = 0xfff00000u;
    struct PageState {
        int32_t v68 = 0, v64 = 0;
        uint8_t v6e = 0;
        std::vector<uint8_t> loc;
    };
    static PageState pageState(const od::port::MessageWindow& w);
    // handles the op at p when it is one of the four; true = p / ret moved
    static bool pageOp(const od::port::MessageWindow& win, PageState& ps, uint32_t& p, uint32_t& ret);
    // pass -1: every window; 0 / 1: the windows of layer (+0x23) >= 3 / < 3 -- the 2D builders that link into OT +0x7c
    // (the shop's / the development room's lists) are drawn between them (layer 3 is OT +0x7c: its windows were
    // linked later in the tick, so drawn first)
    void draw(ui::Canvas& cv, float width, float height, int pass = -1) const;
    // an OpenDash string (UTF-8) in the PS1 Latin font (the PT-BR pack's: its accents), advance as the window text
    void label(ui::Canvas& cv, const char* t, float x, float y, float s, ui::Vec2 o, ui::Color col) const;
    // one string of a system bank (no page header, ends fb 1f / ff): the pack font for the Latin packs, else PC glyphs
    // `width` (not null): only measure the Latin string (320 x 240 px)
    void bankText(ui::Canvas& cv, uint32_t bank, int id, float x, float y, float s, ui::Vec2 o,
                  ui::Color col, float* width = nullptr) const;
    // a Glyph / Text primitive of the sub-screen (game/menu_draw: FUN_00558110 packets, FUN_005578c0 strings). Japanese (or a
    // bank no language pack replaces): the exe's glyphs, 12 px cells from x. A pack's bank: its text, aligned as
    // FUN_005578c0 aligns over 10 cells (1 spread / 3 centred over 120 px from ax, 2 right-aligned at ax + 0x77)
    void menuText(ui::Canvas& cv, const od::port::ScreenPrim& p, float s, ui::Vec2 o) const;
    // one message of a bank as plain text at (x, y) on the 320x240 screen (glyphs, ruby, line feeds; codes skipped)
    void drawString(ui::Canvas& cv, uint32_t bank, int id, float x, float y, float s, ui::Vec2 o,
                    ui::Color col) const;
};

} // namespace od::app
