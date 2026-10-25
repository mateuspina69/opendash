// A TrueType font on the game canvas (the vector UI's captions, prompts, labels): the glyphs rasterized on demand
// (render/ui_font) into one atlas texture, laid out from UTF-8.
#pragma once

#include <filesystem>
#include <string_view>

#include "render/renderer.hpp"
#include "render/ui_canvas.hpp"
#include "render/ui_font.hpp"

namespace od::app {

struct UiText {
    ui::Font font;
    od::TextureHandle tex = 0;
    bool tried = false;

    bool load(od::Renderer& r, const std::filesystem::path& ttf);
    // the glyphs rasterized while this frame's canvas was built, before it is drawn
    void flush(od::Renderer& r);
    bool ready() const { return tex != 0; }

    // em size (window px) of capitals `cap` px high
    float em(float cap) const { return font.emForCapHeight(cap); }
    // the advance of `text` at em `px`
    float width(std::string_view text, float px);
    // `text` with its baseline at y from x; edge > 0: an outline that many px wide in `edgeCol` under it
    void draw(ui::Canvas& cv, std::string_view text, float x, float y, float px, ui::Color col, float edge = 0.0f,
              ui::Color edgeCol = 0);
};

// the next code point of UTF-8 text at i (i advances), 0 at the end
uint32_t nextCodepoint(std::string_view s, size_t& i);

} // namespace od::app
