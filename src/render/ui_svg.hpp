// SVG pictures for the game canvas (the vector UI: a screen's static art, icons, button prompts): parsed once, then
// rasterized (antialiased) at the size they are drawn at, so they stay sharp at any window size.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace od::ui {

class Svg {
public:
    Svg();
    ~Svg();
    Svg(const Svg&) = delete;
    Svg& operator=(const Svg&) = delete;

    bool load(const std::filesystem::path& file);
    bool parse(const std::string& text); // an SVG document in memory
    bool loaded() const;
    float width() const;  // the document's size (its units: for a screen, the 320 x 240 PS1 pixels)
    float height() const;
    // RGBA8 (straight alpha) of the picture scaled by `scale` (out: w x h = ceil(width * scale) x ...)
    std::vector<uint32_t> rasterize(float scale, int& w, int& h) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace od::ui
