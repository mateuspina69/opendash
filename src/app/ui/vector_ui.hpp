// The vector UI (OpenDash, F1 "UI vetorial" / --hd-art 0 = the pixel art): what the 2D screens' pixel art shows,
// redrawn as vector art at the window's resolution -- the containers' pictures (ScreenPrim::vec: assets/ui/*.svg,
// redrawn from the pixel art in the 320 x 240 screen's coordinates), the captions as text in the language's words
// (assets/ui/text/<language>.txt), the button glyphs as prompts of the device in use (app/ui/prompts), the gauges as
// shapes (ScreenPrim::label). What it cannot draw (no picture, no words for the language) stays the pixel art.
#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>

#include "app/ui/ui_text.hpp"
#include "engine/screen_prim.hpp"
#include "render/renderer.hpp"
#include "render/ui_canvas.hpp"
#include "render/ui_svg.hpp"

namespace od::app {

// opendash/assets (the exe is in opendash/build/<config>)
std::filesystem::path assetsDir();

struct VectorUi {
    bool enabled = true;
    // this frame's language ("ptbr", "en", "jp"); its captions are loaded on a change
    void setLanguage(const std::string& code);

    // picture `vec` in the rect x y w h of the 320 x 240 screen (its primitives' bounds; scale s, origin o): the
    // SVG pictures at (x, y), the frames at the rect's size; false = none
    // tint: the sprite's grey level (0x80 = as drawn; the dimmed items' 0x60)
    bool draw(od::Renderer& r, ui::Canvas& cv, uint16_t vec, float x, float y, float w, float h, float s, ui::Vec2 o,
              uint32_t tint = 0x80);
    // primitive p's label (ScreenPrim::label); false = the caller draws the primitive
    bool label(od::Renderer& r, ui::Canvas& cv, const od::port::ScreenPrim& p, float s, ui::Vec2 o);
    // uploads the glyphs this frame's labels rasterized (before the canvas is drawn)
    void flush(od::Renderer& r);

private:
    struct Picture {
        std::unique_ptr<ui::Svg> svg;
        bool tried = false;
        od::TextureHandle tex = 0;
        float scale = 0.0f;
        int w = 0, h = 0;
    };
    std::map<uint16_t, Picture> pics_;
    UiText bold_, regular_;
    std::string lang_;
    std::map<std::string, std::string> words_;
    mutable std::set<std::string> missing_;
    std::set<std::string> missingAt_;
    // the caption `key` in this language; null = none (the pixel art stays)
    const std::string* word(const std::string& key) const;
    bool fonts(od::Renderer& r);
    // the text drawn on picture `vec` (the letters of its static art)
    void pictureText(ui::Canvas& cv, uint16_t vec, float s, ui::Vec2 o);
    // the 9-slice frames (kVecFrame0..2): an SVG at the call's size (frameSvg), cached by style and size
    struct Frame {
        std::unique_ptr<ui::Svg> svg;
        bool tried = false;
        od::TextureHandle tex = 0;
        float scale = 0.0f;
        int w = 0, h = 0;
    };
    std::map<std::string, Frame> frames_;
    static std::string frameSvg(int style, float w, float h); // empty = a style not drawn yet
    bool frame(od::Renderer& r, ui::Canvas& cv, int style, float x, float y, float w, float h, float s, ui::Vec2 o);
};

} // namespace od::app
