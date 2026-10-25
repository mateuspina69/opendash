// A TrueType font for the game canvas (render/ui_canvas.hpp): glyphs rasterized on demand at the pixel size they are
// drawn at (sharp at any window size), packed into one RGBA atlas (white, alpha = coverage). A glyph can also be
// asked with an outline: its coverage grown by r pixels (drawn under the fill, in another colour).
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <vector>

namespace od::ui {

class Font {
public:
    Font();
    ~Font();
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    bool load(const std::filesystem::path& ttf);
    bool loaded() const { return !data_.empty(); }
    bool has(uint32_t codepoint) const;
    // em size of a font whose capital letters are `cap` pixels high
    float emForCapHeight(float cap) const;

    struct Glyph {
        bool ok = false;
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0; // the bitmap's rect from the pen on the baseline (pixels, y down)
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0; // in the atlas
        float advance = 0;                    // pixels
    };
    // the glyph at em size `px` pixels, its coverage grown by `outline` pixels (0 = the plain glyph)
    const Glyph& glyph(uint32_t codepoint, int px, int outline);
    // the kerning between a and b (pixels at em size `px`, added to a's advance)
    float kern(uint32_t a, uint32_t b, int px) const;

    static constexpr int kAtlas = 2048;
    const std::vector<uint32_t>& atlas() const { return atlas_; }
    // glyphs were added since the last upload (the host uploads atlas() then clears it)
    bool dirty = false;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::vector<uint8_t> data_;
    std::vector<uint32_t> atlas_;
    std::map<uint64_t, Glyph> glyphs_;
    int shelfX_ = 0, shelfY_ = 0, shelfH_ = 0;
    void reset();
};

} // namespace od::ui
