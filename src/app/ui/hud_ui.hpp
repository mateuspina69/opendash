// The field HUD (game/hud) drawn on the game canvas (render/ui_canvas.hpp) on the 320 x 240 PS1 screen.
#pragma once

#include "render/ui_canvas.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "content/late_game/end_scripts.hpp"
#include "game/hud.hpp"
#include "render/renderer.hpp"

namespace od::app {

// The field HUD (game/hud) on the 320x240 PS1 screen, scaled to the window height and centred like MessageUi.
// Frames sample tpage 0x0d (decoded from INIT.DAT + GAME.DAT) with the element's clut; the page is uploaded
// 4x nearest-upscaled so the UI's linear sampler keeps the pixel art sharp.
// the HD credit page `name` (p00.., x01..) of language `lang` (else English); empty = none
std::filesystem::path creditPage(const std::string& name, const std::string& lang);
// how many pages OpenDash adds to the staff roll (x01.. in a row)
int creditExtraPages(const std::string& lang);
// OpenDash (Herbert): the credits' cards (assets/ui/credits/cNN_<lang>.png, in the order they pass): how many
int creditCards(const std::string& lang);
// the gallery's pictures (assets/ui/gallery/gNN.png): how many
int galleryPictures();
// what the credits show at t seconds of a song `song` seconds long: the card (-1 none), its and the background's
// opacity; done = the song is over
struct CreditsFrame {
    int card = -1;
    float alpha = 0.0f, background = 0.0f;
    bool done = false;
};
CreditsFrame creditsFrame(double t, double song, int cards);

struct HudUi {
    std::map<uint32_t, od::TextureHandle> tex; // tpage << 16 | clut -> page texture
    bool tried = false;
    uint32_t vramGen = 0; // g_roomVramGen the pages were decoded from (0 = INIT.DAT + GAME.DAT only)
    void load(od::Renderer& r);
    void draw(ui::Canvas& cv, const std::vector<od::hud::Prim>& prims, float width, float height) const;
    // the staff roll (script 0x6e): its sprites composed as the GPU blends them on the 320 x 240 screen
    void drawStaff(ui::Canvas& cv, od::Renderer& r, const od::port::StaffPicture& pic, float width, float height);
    // the credits (the ending's and the title options'): the dimmed illustration, the card of this frame
    void drawCredits(ui::Canvas& cv, od::Renderer& r, const CreditsFrame& f, float width, float height);
    // OpenDash (Herbert): the gallery (the title options' Galeria row): illustration `index` of assets/ui/gallery
    // (gNN.png, `count` of them) over the credits' dimmed background, its number under it
    // side (Herbert: the cast's cards): the picture smaller, on the left of the middle -- its text goes on the right
    void drawGallery(ui::Canvas& cv, od::Renderer& r, const std::filesystem::path& picture, int index, int count,
                     float width, float height, bool side = false);
    od::TextureHandle staffTex{};
    od::port::StaffPicture staffShown;
    // OpenDash's HD credit pages (assets/ui/credits: p<NN> the game's text pages again, x<NN> its own) in this language
    std::string creditsLang = "en";
    std::map<std::string, std::pair<od::TextureHandle, float>> creditTex; // path -> texture, aspect
    od::TextureHandle creditTexture(od::Renderer& r, const std::filesystem::path& path, float& aspect);
    std::vector<uint32_t> staffPixels;
};

} // namespace od::app
