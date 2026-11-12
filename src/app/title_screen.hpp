// The boot / title flow (game/title) outside the game loop: the main task and the title task.
#pragma once

#include "render/ui_canvas.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "app/menu_vram.hpp"
#include "app/ui/prim_painter.hpp"
#include "engine/screen_prim.hpp"
#include "game/save_menus.hpp"
#include "game/title.hpp"
#include "game/fade.hpp"
#include "render/renderer.hpp"
#include "render/ui_svg.hpp"
#include <memory>

namespace od::app {

// ---- the boot / title flow (game/title): the main task and the title task outside the game loop ---------------------
// OpenDash starts here (unless --skip-title / --newgame / --load / --play / --stage): mode 0 (boot), the Capcom
// logo, the attract intro (the game loop on ST02, scripts 3 / 4), the title ("PRESS ENTER", the menu: new game /
// continue (the load menu) / tutorial / options), the demo. The tasks run on PlayerView's loaded files; a switch to the
// game loop starts PlayerView's play on the state the title left (startGameFromState); while the game loop runs, a
// change of *0xa7a000 (the attract's end writes 2) hands the frame back to the main task.
struct TitleScreen {
    od::port::TitleTasks tasks;
    od::port::TitleHost host;
    bool active = false;      // the main task / the title task runs (no PlayerView play)
    bool inGame = false;      // the game loop was started by the main task
    uint32_t gameMode = 0;    // *0xa7a000 (u16) when it was started
    std::vector<od::port::ScreenPrim> prims; // the last frame's primitives
    MenuVram mv;              // the VRAM the main task's DAT loads built (INIT, LOGO, GAME, DEMO, TITLE, G_OVER00,
                              // the options' SUBSCN06): the title's sprites and the sub-screen's (game/status_options)
    std::map<uint32_t, od::TextureHandle> tex; // tpage << 16 | clut -> 1024 x 1024 (4x) page
    std::filesystem::path gameDir, savesDir;
    bool demoShown = false;    // FUN_00542530 ran this frame: the "DEMO" sprite
    std::string error;
    uint64_t frames = 0;

    void start(uint8_t mode);
    // FUN_00542460 for the demo (0xa6d18e == 2): the stage texture DAT's type-2 segment (the pad recording; the
    // container loads type 2 to 0x6d000d ? 0x784800 : 0x7a8800) and the replay's start
    void startDemo();
    // the "DEMO" sprite of FUN_00542530 (320 x 240 screen, semi-transparent 50 %)
    void drawDemo(od::Renderer& r, ui::Canvas& cv, float width, float height);
    // a slot of the load menu: OpenDash's own saves first, else the game's (read only)
    std::filesystem::path slotPath(uint32_t slot) const;
    void bindHost(od::port::FadeTask* fade);
    void applyDat(const std::string& name);
    // the menu row (ScreenPrim::row) drawn under window pixel (x, y) by the last frame, -1 = none
    int rowAt(float width, float height, float x, float y) const;
    std::vector<od::TextureHandle> stale;
    void dropTextures();
    // Maybe_ProcessPendingDatLoads (WinMain) for the main task's loads: VRAM segments into the screen VRAM, the
    // type-9 MP3 (the title music) into slot 0, the type-6 message bank
    void processDats();

    od::TextureHandle page(od::Renderer& r, uint16_t tpage, uint16_t clut);
    // the 2D screen: black, then the primitives in the display's resolution (0xa7172c: 2 = 640 x 480, else 320 x
    // 240) fitted to the window height (4:3, centred); `text` draws the Glyph / Text primitives (the options page)
    PrimPainter painter;
    // OpenDash: the Capcom logo (game/title drawLogo) as vector art (assets/ui/capcom_logo.svg, traced from LOGO.DAT's
    // sprites) at the window's resolution, its white over the whole window; false = not the logo's frame / no picture
    struct LogoArt {
        std::unique_ptr<ui::Svg> svg;
        bool tried = false;
        od::TextureHandle tex = 0;
        float scale = 0.0f;
        int w = 0, h = 0;
        od::TextureHandle png = 0; // assets/ui/capcom_logo.png (the official logo; first choice over the SVG)
        int pngW = 0, pngH = 0;
    } logoArt;
    bool drawLogoArt(od::Renderer& r, ui::Canvas& cv, float width, float height);
    // OpenDash (the vector UI, painter.art.enabled): the title in high resolution. The logo's sprites (game/title
    // drawTitleBackground: clut 0x7dc0) give way to the game's logo as a picture with "OPEN DASH EDITION" drawn into it
    // (assets/ui/title/logo_mml2.png; logo_rd2.png -- Rockman Dash 2 -- in Japanese, logo_rd2s.png -- without the
    // Japanese subtitle -- in Korean / Chinese), fitted to the sprites' box, resized from its master to its size; any
    // other 4-bit sprite whose pixels have a vector picture (assets/ui/title/sprite_<hash>.svg: the menu's words, the
    // cursor, PRESS START -- traced from the pixel art, hash = spriteHash) is drawn from it. OD_DUMP_TITLE=<dir> writes
    // each sprite without one as <dir>/sprite_<hash>.txt (its indices and palette: the tracer's input)
    std::string uiLang; // this frame's text language (lang::uiTextCode; app_frame sets it)
    struct HdImage {
        std::vector<uint8_t> src; // RGBA8 master
        int sw = 0, sh = 0;
        bool tried = false;
        od::TextureHandle tex = 0;
        int w = 0, h = 0;
    };
    std::map<std::string, HdImage> hdImages;
    struct HdSprite {
        std::unique_ptr<ui::Svg> svg;
        bool tried = false;
        od::TextureHandle tex = 0;
        float scale = 0.0f;
        int w = 0, h = 0;
    };
    std::map<uint64_t, HdSprite> hdSprites;
    // FNV-1a 64 of a 4-bit sprite's indices (row by row, one byte each) then its 16 palette words (low byte first)
    uint64_t spriteHash(const od::port::ScreenPrim& p) const;
    // the master `file` resized to w x h (cached until the size changes); null = no picture
    HdImage* hdImage(od::Renderer& r, const std::string& file, int w, int h);
    // the pictures that stand for this frame's logo / sprites (drawn over the painter's pass, window pixels); `rest` =
    // the primitives the painter still draws
    struct HdDraw {
        od::TextureHandle tex;
        ui::Vec2 a, b;
        ui::Color c;
    };
    std::vector<HdDraw> hdTitle(od::Renderer& r, float s, ui::Vec2 o, std::vector<od::port::ScreenPrim>& rest);
    // OpenDash: the menu's Extras row (kLblTitleExtras) as its word's picture; false = no picture
    bool extrasWord(od::Renderer& r, const od::port::ScreenPrim& p, float s, ui::Vec2 o, HdDraw& out);
    void draw(od::Renderer& r, ui::Canvas& cv, float width, float height,
              const std::function<void(const od::port::ScreenPrim&, float, ui::Vec2)>& textAt);
};

} // namespace od::app
