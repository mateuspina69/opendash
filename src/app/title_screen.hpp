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
    void draw(od::Renderer& r, ui::Canvas& cv, float width, float height,
              const std::function<void(const od::port::ScreenPrim&, float, ui::Vec2)>& textAt);
};

} // namespace od::app
