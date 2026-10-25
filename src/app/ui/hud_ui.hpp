// The field HUD (game/hud) drawn on the game canvas (render/ui_canvas.hpp) on the 320 x 240 PS1 screen.
#pragma once

#include "render/ui_canvas.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <vector>

#include "game/hud.hpp"
#include "render/renderer.hpp"

namespace od::app {

// The field HUD (game/hud) on the 320x240 PS1 screen, scaled to the window height and centred like MessageUi.
// Frames sample tpage 0x0d (decoded from INIT.DAT + GAME.DAT) with the element's clut; the page is uploaded
// 4x nearest-upscaled so the UI's linear sampler keeps the pixel art sharp.
struct HudUi {
    std::map<uint32_t, od::TextureHandle> tex; // tpage << 16 | clut -> page texture
    bool tried = false;
    uint32_t vramGen = 0; // g_roomVramGen the pages were decoded from (0 = INIT.DAT + GAME.DAT only)
    void load(od::Renderer& r);
    void draw(ui::Canvas& cv, const std::vector<od::hud::Prim>& prims, float width, float height) const;
};

} // namespace od::app
