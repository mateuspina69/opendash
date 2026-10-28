// The boot / title flow (see title.hpp), one function per exe function; every write keeps the exe's width (checked in
// the disassembly where the decompiler was ambiguous). The draw-only callees build ScreenPrim lists.
#include "game/content_registry.hpp"
#include "game/title.hpp"
#include "game/title_state.hpp"

#include <stdexcept>
#include <string>

#include "game/actor_pools.hpp"
#include "game/actors.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/display.hpp"
#include "game/fade.hpp"
#include "game/fade_state.hpp"
#include "game/game_state.hpp"
#include "game/menu_draw.hpp"
#include "game/menu_pointer.hpp"
#include "game/message.hpp"
#include "game/player/event_loadout.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_model.hpp"
#include "game/room_grid.hpp"
#include "game/room_params.hpp"
#include "game/save_menus.hpp"
#include "game/save_slots.hpp"
#include "game/stage_files.hpp"
#include "game/stage_state.hpp"
#include "game/status_screen.hpp"
#include "game/story_flags.hpp"
#include "game/transition_state.hpp"
#include "game/dat_names.hpp"

using namespace od::port::title_state;

namespace od::port {

namespace {

TitleTasks* T = nullptr;
TitleHost* H = nullptr;
std::vector<ScreenPrim>* D = nullptr;

// the DATs the flow loads (the DAT ids of exe 0x40efe0)
constexpr DatFile kDemoDat{"DEMO.DAT"}, kGameDat{"GAME.DAT"}, kGameOverDat{"G_OVER00.DAT"}, kInitDat{"INIT.DAT"}, kLogoDat{"LOGO.DAT"}, kTitleDat{"TITLE.DAT"};
// the boot's draw defaults: the depth offset, the depth ranges / the effects' far depths by coordinate shift, the LOD
// distances
constexpr uint16_t kBootDepthOffset = 1;
constexpr uint16_t kBootDepthRanges[3] = {0xe00, 0x700, 0x380};
constexpr int16_t kBootLodDistances[4] = {0xa00, 0xb00, 0x1600, 0x180};
constexpr uint8_t kNormalDifficulty = 1;
constexpr uint32_t kNoBestTime = 0xffffffff;
// the brightness of the pictures: full, and its steps
constexpr int16_t kFullBrightness = 0x80;
constexpr int16_t kLogoBrightnessStep = 4;
constexpr uint16_t kTitleBrightnessStep = 2, kMenuBrightnessStep = 4;
// the logo's hold (pace units) and the scan steps' timer
constexpr int16_t kLogoHold = 0xb4;
constexpr uint16_t kScanTicks = 0x10;
// "press enter"'s idle before the attract / the demo, and the cursor's slide: (3 - pace) * these
constexpr uint16_t kIdleTicksPerPace = 0x100, kSlideTicksPerPace = 0x400;
constexpr uint8_t kCursorSlideTicks = 3;
constexpr uint8_t kPaceSteps = 3; // the pace's complement the timers scale by
// the menu's rows / the difficulty choice's last row
constexpr int kMenuLastRow = 3;
constexpr uint16_t kMaxDifficultyRows = 3;
constexpr uint16_t kTitleMusic = 0;
constexpr uint16_t kGameOverSound = 0xc;
// the music fades the flow asks for (step, delay 0)
constexpr int16_t kTitleMusicFadeStep = 0x111, kGameOverMusicFadeStep = 0xb6;
// the runs the title starts: the stage / room / story bytes of a new game, the tutorial, the attract and the demo
constexpr uint8_t kNewGameStage = 0x39, kTutorialStage = 0x4a, kAttractStage = 2, kDemoStage = 0x51;
constexpr uint8_t kDemoRoom = 1, kDemoStory = 3;
// the attract toggle's runs
constexpr uint8_t kAttractRun = 0, kDemoRun = 1;
// the game over's rescue: fade 2 after the load and none before it
constexpr uint8_t kRescueFadeIn = 2;

// exe 0x604b06(task, entry): the entry from the next frame on
void taskSwitch(TaskEntry e) {
    T->pending = e;
    T->switched = true;
}
// exe 0x53f6b0: the fade request on the app's fade task
void fade(uint32_t request, uint8_t param) {
    if (!H->fade) throw std::logic_error("title port: no fade task");
    fadeStart(*H->fade, request, param);
}
// the timers that scale by the pace: (3 - pace) * unit
uint16_t paceTicks(uint16_t unit) { return uint16_t((uint32_t(kPaceSteps) - display().pace) * unit); }

// ---- draw (the packet builders; only when the display list is open) --------------------------------------------------
bool drawing() { return D && display().drawingOff == 0; }
int16_t gTitleRow = -1; // OpenDash: the menu row (ScreenPrim::row, the mouse's target) of the next sprite
// a sprite of the title's display
void sprite(int x, int y, int w, int h, int u, int v, uint16_t tpage, uint16_t clut, uint32_t col) {
    ScreenPrim p;
    p.row = gTitleRow;
    gTitleRow = -1;
    p.kind = ScreenPrim::Sprite;
    p.x = int16_t(x);
    p.y = int16_t(y);
    p.w = int16_t(w);
    p.h = int16_t(h);
    p.u = uint8_t(u);
    p.v = uint8_t(v);
    p.tpage = tpage;
    p.clut = clut;
    p.colour = col & 0xffffff;
    D->push_back(p);
}
// a full-width G4 band from y0 to y1, `top` above, `bottom` below
void gouraud(int y0, int y1, uint32_t top, uint32_t bottom) {
    ScreenPrim p;
    p.kind = ScreenPrim::Gouraud;
    const int16_t xs[4] = {0, 0x280, 0, 0x280}, ys[4] = {int16_t(y0), int16_t(y0), int16_t(y1), int16_t(y1)};
    for (int k = 0; k < 4; ++k) {
        p.qx[k] = xs[k];
        p.qy[k] = ys[k];
        p.qc[k] = (k < 2 ? top : bottom) & 0xffffff;
    }
    D->push_back(p);
}
// the grey of a brightness (the colour of the pictures)
uint32_t grey(uint32_t b) { return b | (b << 8) | (b << 16); }
// the menu cursor's wobble: the 64-step sine by the frame, >> 10
int cursorWobble() { return fixedSin64(gameState().frame) >> 10; }

// exe 0x4299b0: the title background (blue -> light cyan gradient, three G4 bands) and the logo (four 8-bit sprites of
// tpages 0x9a..0x9d, clut 0x7dc0, 128 x 224 / 96 x 224 at (0x50 + i * 0x80, 0x20)).
// OpenDash (widescreen): the bands continue past the 640 frame (a drawing area opened 2 * screenExtra() px each side).
// Language packs (setTitlePs1Layout): the PS1 title overlay's logo (DEMO.BIN 0x800ad900: five 8-bit sprites of tpages
// 0x9a..0x9e, 128 x 240 (the last 32 x 240) at (0x30 + i * 0x80, 0x20), clut 0x7dc0 -- "MEGA MAN LEGENDS 2" is
// 544 x 240)
bool gPs1Layout = false;
void drawTitleBackground() {
    if (!drawing()) return;
    const uint32_t c = titleBlock().brightness, full = (c * 0xff) >> 7;
    const uint32_t top = (((full & 0xffff) << 8 | ((c * 0x47) >> 7)) << 8 | ((c * 0x45) >> 7)) & 0xffffff;
    const uint32_t bottom = (((full & 0xffff) << 8 | full) << 8 | ((c * 0xcf) >> 7)) & 0xffffff;
    const int ext = screenExtra() * 2;
    if (ext > 0) {
        ScreenPrim clip;
        clip.kind = ScreenPrim::Clip;
        clip.x = int16_t(-ext);
        clip.y = 0;
        clip.w = int16_t(0x280 + 2 * ext);
        clip.h = 0x1e0;
        D->push_back(clip);
    }
    auto band = [&](int y0, int y1, uint32_t t, uint32_t bt) {
        gouraud(y0, y1, t, bt);
        for (int k = 0; k < 4; ++k) D->back().qx[k] = int16_t((k & 1) ? 0x280 + ext : -ext);
    };
    band(0, 0x80, top, top);
    band(0x80, 0x160, top, bottom);
    band(0x160, 0x1e0, bottom, bottom);
    if (gPs1Layout) {
        for (int i = 0; i < 5; ++i)
            sprite(0x30 + i * 0x80, 0x20, i < 4 ? 0x80 : 0x20, 0xf0, 0, 0, uint16_t(0x290 | (0xa + i)), 0x7dc0, grey(c));
        return;
    }
    for (int i = 0; i < 4; ++i)
        sprite(0x50 + i * 0x80, 0x20, i < 3 ? 0x80 : 0x60, 0xe0, 0, 0, uint16_t(0x290 | (0xa + i)), 0x7dc0, grey(c));
}
// exe 0x429bb0: the copyright (tpage 0xa, clut 0x7c02, two 256 x 24 halves at y 0x1a0) and, while the timer's bit 5,
// "PRESS ENTER" (tpage 0xb, clut 0x7c41)
void drawPressEnter() {
    if (!drawing()) return;
    TitleBlock& t = titleBlock();
    const uint32_t col = grey(t.brightness);
    if (gPs1Layout) { // the PS1 overlay (DEMO.BIN 0x800adbd0): its copyright is 256 x 48 + 256 x 32 at (0, 0xb0) / (0, 0xe0)
        sprite(0x40, 0x194, 0x100, 0x30, 0, 0xb0, 0x20a, 0x7c02, col);
        sprite(0x140, 0x1a0, 0x100, 0x20, 0, 0xe0, 0x20a, 0x7c02, col);
    } else {
        sprite(0x40, 0x1a0, 0x100, 0x18, 0, 0xd0, 0x20a, 0x7c02, col);
        sprite(0x140, 0x1a0, 0x100, 0x18, 0, 0xe8, 0x20a, 0x7c02, col);
    }
    if (uint8_t(t.ticks) & 0x20) {
        sprite(0x8c, 0x150, 0x100, 0x28, 0, 0xb0, 0x20b, 0x7c41, col);
        sprite(0x18c, 0x150, 0x68, 0x28, 0, 0xd8, 0x20b, 0x7c41, col);
    }
}
// exe 0x429d00: the four menu items (216 x 40 at (0xd4, 0x110 + i * 0x28), the cursor's row tpage 0xa / clut 0x7c00, the
// others tpage 0xb / clut 0x7c40) and the cursor arrow (32 x 32, uv (0xe0, 0), clut 0x7c01) sliding between the rows
// (the slide's countdown, the previous row)
void drawMenu() {
    if (!drawing()) return;
    TitleBlock& t = titleBlock();
    const uint32_t col = grey(t.brightness);
    const int cur = t.menuRow;
    for (int i = 0; i < 4; ++i) {
        const bool on = i == cur;
        gTitleRow = int16_t(i);
        sprite(0xd4, 0x110 + i * 0x28, 0xd8, 0x28, 0, i * 0x28, on ? 0x20a : 0x20b, on ? 0x7c00 : 0x7c40, col);
    }
    const int row = (int(t.previousRow) - cur) * int(int8_t(t.slideTicks)) + cur * 4;
    sprite(cursorWobble() + 0xac, 0x114 + row * 10, 0x20, 0x20, 0xe0, 0, 0x20a, 0x7c01, col);
}
// the difficulty choice's last row: min(the hardest difficulty cleared, 3)
uint32_t difficultyLastRow() {
    uint32_t n = gameState().bestDifficulty;
    if (n > kMaxDifficultyRows) n = kMaxDifficultyRows;
    return n;
}
// exe 0x429ec0: the difficulty list (tpage 0xc; 1 + difficultyLastRow() rows; the chosen row clut 0x7c80, the others
// 0x7c83 + v 0x78) and its cursor
void drawDifficulty() {
    if (!drawing()) return;
    TitleBlock& t = titleBlock();
    const uint32_t n = difficultyLastRow();
    const int base = int(n) * -0x14;
    const uint32_t col = grey(t.brightness);
    const int cur = t.difficultyRow;
    // rows {y, w, x}: two always, the third when a difficulty past the first was cleared, the fourth past the second;
    // uv words: chosen {0x7c800000, 0x7c800080, 0x7c802800, 0x7c805000}, else {0x7c837800, 0x7c837880, 0x7c83a000,
    // 0x7c83c800}
    static constexpr int kY[4] = {0x14c, 0x174, 0x19c, 0x1c4}, kW[4] = {0x80, 0x60, 0xa0, 0xa0},
                         kX[4] = {0x100, 0x110, 0xf0, 0xf8};
    static constexpr uint8_t kU[4] = {0x00, 0x80, 0x00, 0x00}, kVon[4] = {0x00, 0x00, 0x28, 0x50},
                             kVoff[4] = {0x78, 0x78, 0xa0, 0xc8};
    // the PS1 overlay's rows (DEMO.BIN 0x800ae000: its words EASY / HARD on one line, NORMAL, VERY HARD): {x, w, u}
    // and v chosen / not; the same y
    static constexpr int kPx[4] = {0x104, 0xe0, 0x100, 0xd8}, kPw[4] = {0x78, 0xc0, 0x80, 0xd0};
    static constexpr uint8_t kPu[4] = {0x00, 0x00, 0x78, 0x00}, kPvOn[4] = {0x00, 0x28, 0x00, 0x50},
                             kPvOff[4] = {0x78, 0xa0, 0x78, 0xc8};
    const uint16_t cleared = gameState().bestDifficulty;
    const uint32_t rows = cleared > 2 ? 4 : cleared > 1 ? 3 : 2;
    for (uint32_t i = 0; i < rows; ++i) {
        const bool on = int(i) == cur;
        gTitleRow = int16_t(i);
        if (gPs1Layout)
            sprite(kPx[i], base + kY[i], kPw[i], 0x28, kPu[i], on ? kPvOn[i] : kPvOff[i], 0x20c, on ? 0x7c80 : 0x7c83, col);
        else
            sprite(kX[i], base + kY[i], kW[i], 0x28, kU[i], on ? kVon[i] : kVoff[i], 0x20c, on ? 0x7c80 : 0x7c83, col);
    }
    const int row = (int(t.previousRow) - cur) * int(int8_t(t.slideTicks)) + cur * 4;
    sprite(cursorWobble() + 0xd0, base + 0x150 + row * 10, 0x20, 0x20, 0xe0, 0, 0x20a, 0x7c01, col);
}
// exe 0x42a120: game over (a dark blue G4 + four 8-bit sprites 128 x 64 / 112 x 64 of tpages 0x9a..0x9d, clut 0x7c00, at
// (0x48 + i * 0x80, 0xd0))
void drawGameOver() {
    if (!drawing()) return;
    const uint32_t c = titleBlock().brightness, blue = ((c * 0xe) >> 7) << 16;
    gouraud(0, 0x1e0, blue, blue);
    for (int i = 0; i < 4; ++i)
        sprite(0x48 + i * 0x80, 0xd0, i < 3 ? 0x80 : 0x70, 0x40, 0, 0, uint16_t(0x290 | (0xa + i)), 0x7c00, grey(c));
}
// exe 0x543510: the Capcom logo (a white F rect, three 4-bit sprites of tpages 0xa..0xc, clut 0x7c00, 256 x 96 / 256 x
// 96 / 32 x 96 at (0x30 + i * 0x100, 0xc0))
void drawLogo() {
    if (!D) return;
    const uint32_t c = grey(uint32_t(int32_t(mainTask().brightness)));
    ScreenPrim bg;
    bg.kind = ScreenPrim::Flat;
    bg.x = 0;
    bg.y = 0;
    bg.w = 0x280;
    bg.h = 0x1e0;
    bg.colour = (c * 2 - 0x10101u) & 0xffffff;
    D->push_back(bg);
    sprite(0x30, 0xc0, 0x100, 0x60, 0, 0, 0x20a, 0x7c00, c);
    sprite(0x130, 0xc0, 0x100, 0x60, 0, 0, 0x20b, 0x7c00, c);
    sprite(0x230, 0xc0, 0x20, 0x60, 0, 0, 0x20c, 0x7c00, c);
}

// ---- the main task's modes [0x8ae900] ------------------------------------------------------------------------------
// exe 0x543010: the boot (INIT.DAT, the defaults)
void bootMode() {
    // exe 0x40b760(1): the display set-up (renderer)
    datLoadRequest(kInitDat);
    StageState& st = stage();
    st.coordinateShift = 0;
    roomGrid().clearPlacementBytes();
    st.depthOffset = kBootDepthOffset;
    st.depthRange = kBootDepthRanges[0];
    RoomDrawState& draw = roomDraw();
    for (size_t i = 0; i < 3; ++i) {
        draw.effectsFar[i] = kBootDepthRanges[i];
        draw.depthRanges[i] = kBootDepthRanges[i];
    }
    for (size_t i = 0; i < 4; ++i) st.lodDistances[i] = kBootLodDistances[i];
    titleBlock().attractToggle = kAttractRun;
    od::playerFilesForget();
    st.bankBuffer = 0;
    st.spareBuffer = 0;
    GameState& g = gameState();
    g.savesExist = 0;
    g.bestTime = kNoBestTime;
    g.clears = 0;
    g.difficulty = kNormalDifficulty;
    GameOptions& options = gameOptions();
    options.minimapShown = 1;
    options.clearControllerWord();
    g.frontEnd = kFrontEndNone;
    display().frontEndLayer = 1; // exe 0x549a70(1)
    mainTask().mode += 1;
}
// exe 0x543120: the Capcom logo (LOGO.DAT), the save scan, then the attract (ToGame, ST02)
void logoMode() {
    MainTaskState& m = mainTask();
    switch (LogoStep(m.step)) {
    case LogoStep::DisplayMode:
        if (display().drawBuffer == 0) return;
        setBufferMode(kBufferModeFrontEnd);
        drawEnvironments().clearBackground = 1;
        setDisplayMode(kTitleDisplayMode);
        m.step += 1;
        return;
    case LogoStep::LoadLogo:
        datLoadRequest(kLogoDat);
        m.step += 1;
        return;
    case LogoStep::FadeIn:
        if (fadeState().busy != 0 || (fileLoads().loading & kFileLoading)) return;
        fade(kFadeInRequest, 0);
        m.brightness = 0;
        m.step += 1;
        [[fallthrough]];
    case LogoStep::Brighten:
        m.brightness = int16_t(m.brightness + kLogoBrightnessStep);
        if (m.brightness > kFullBrightness) {
            m.brightness = kFullBrightness;
            m.logoTicks = kLogoHold;
            m.step += 1;
        }
        drawLogo();
        return;
    case LogoStep::ScanSaves: {
        const uint32_t saves = H->scanSaves ? H->scanSaves() : 0; // exe 0x548910
        saveSlotBits().present = saves;
        gameState().savesExist = saves != 0 ? 1u : 0u;
        m.scanTicks = kScanTicks;
        m.step += 2;
        drawLogo();
        m.logoTicks = int16_t(m.logoTicks - 1);
        return;
    }
    case LogoStep::Advance5:
    case LogoStep::Advance7:
        m.step += 1;
        drawLogo();
        m.logoTicks = int16_t(m.logoTicks - 1);
        return;
    case LogoStep::RescanTimer:
        m.scanTicks = kScanTicks;
        m.step += 2;
        drawLogo();
        m.logoTicks = int16_t(m.logoTicks - 1);
        return;
    case LogoStep::Shown: {
        drawLogo();
        if (!(pad0().pressed & kPadConfirm)) {
            m.logoTicks = int16_t(m.logoTicks - 1);
            if (m.logoTicks >= 0) return;
        }
        fade(kWipe20Request + 2, 0);
        GameState& g = gameState();
        g.loadSave = 0;
        g.stage = kAttractStage;
        g.room = 0;
        g.story = 0;
        g.nextStory = 0;
        g.flutterDock = 0;
        g.playerKind = 0;
        m.switchMode(MainMode::ToGame);
        return;
    }
    default: return;
    }
}
// the modes that load a DAT: ToTitle / ToGameOver (exe 0x5432b0 / 0x543490: DEMO.DAT, then the title task at the state
// `title`) and ToDemo / ToGame (exe 0x543350 / 0x5433f0: GAME.DAT, then the game loop with the front end `frontEnd`)
void loadMode(uint16_t dat, bool toTitle, TitleState title, uint8_t frontEnd) {
    MainTaskState& m = mainTask();
    switch (MainLoadStep(m.step)) {
    case MainLoadStep::WaitBuffer:
        if (display().drawBuffer == 0) return;
        setBufferMode(kBufferModeFrontEnd);
        m.step += 1;
        return;
    case MainLoadStep::RequestDat:
        datLoadRequest(dat);
        m.step += 1;
        return;
    case MainLoadStep::WaitDat: {
        if (fileLoads().loading & kFileLoading) return;
        GameState& g = gameState();
        if (toTitle) {
            if (title == TitleState::Load) { // ToTitle only: the other buffers
                StageState& st = stage();
                st.bankBuffer ^= 1u;
                st.spareBuffer ^= 1u;
            }
            g.frontEnd = kFrontEndTitle;
            titleBlock().restart(title);
            taskSwitch(TaskEntry::Title);
        } else {
            od::playerFilesForget();
            g.frontEnd = frontEnd;
            g.clearModeBlock();
            taskSwitch(TaskEntry::GameLoop);
        }
        return;
    }
    default: return;
    }
}

// ---- the title task's states [0x88c4a0] ----------------------------------------------------------------------------
// the stage / story bytes a run starts with: a new game (not loaded), the entry mode, the stage and room, the story and
// the next story, the dock and the player's kind cleared, the difficulty
void setRun(EntryMode entry, uint8_t stageId, uint8_t room, uint8_t story, uint8_t nextStory, uint8_t difficulty) {
    GameState& g = gameState();
    g.loadSave = 0;
    g.entryMode = uint8_t(entry);
    g.stage = stageId;
    g.room = room;
    g.story = story;
    g.nextStory = nextStory;
    g.flutterDock = 0;
    g.playerKind = 0;
    g.difficulty = difficulty;
}
// a run started from the title: the fade's busy byte cleared, the wipe, the main task to ToGame
void startRun() {
    fadeState().busy = 0;
    fade(kWipe26Request, 0);
    mainTask().switchMode(MainMode::ToGame);
    taskSwitch(TaskEntry::Main);
}
// exe 0x428f00: TITLE.DAT, then PressEnter (or Menu when asked)
void titleLoad() {
    TitleBlock& t = titleBlock();
    switch (TitleLoadStep(t.step)) {
    case TitleLoadStep::WaitSounds:
        if (!soundDone(kMusicFades)) return;
        // exe 0x5d4f30: empty
        display().frontEndLayer = 1; // exe 0x549a70(1)
        t.step += 1;
        return;
    case TitleLoadStep::RequestDat:
        datLoadRequest(kTitleDat);
        t.step += 1;
        return;
    case TitleLoadStep::SetUp:
        if (fadeState().busy != 0 || display().drawBuffer == 0) return;
        drawEnvironments().clearBackground = 1;
        setDisplayMode(kTitleDisplayMode);
        // exe 0x5f3200 (TITLE.DAT's type-1 segment walk), exe 0x5e00e0(1) (pools reset), exe 0x5e6440(0x88c400: empty): no
        // title actors; the class table and the depth cue pointers as the exe sets them
        stageClassTables().animated.pointAt(TableId::TitleClassTables);
        stage().depthCue.pointAt(TableId::TitleDepthCue);
        t.ticks = 0;
        t.ticksB = 0;
        t.menuRow = int8_t(gameState().savesExist);
        t.difficultyRow = 0;
        t.enter(t.straightToMenu != 0 ? TitleState::Menu : TitleState::PressEnter);
        t.clearArgs();
        return;
    default: return;
    }
}
// exe 0x428fe0: "PRESS ENTER"; idle -> the attract (ST02) / the demo (ST51 sub 1) alternately
void titlePressEnter() {
    TitleBlock& t = titleBlock();
    const PressEnterStep step = PressEnterStep(t.step);
    if (step == PressEnterStep::FadeIn) {
        if (fadeState().busy != 0 || (fileLoads().loading & kFileLoading)) return;
        fade(kFadeInRequest, 0);
        t.brightness = 0;
        t.step += 1;
    }
    if (step <= PressEnterStep::Brighten) {
        t.brightness += kTitleBrightnessStep;
        if (t.brightness > uint16_t(kFullBrightness)) {
            t.brightness = uint16_t(kFullBrightness);
            t.ticks = paceTicks(kIdleTicksPerPace);
            t.step += 1;
        }
        drawTitleBackground();
        drawPressEnter();
    } else if (step == PressEnterStep::Idle) {
        drawTitleBackground();
        drawPressEnter();
        t.ticks -= 1u;
        if (t.ticks == 0) {
            fade(kWipe20Request, 0);
            const uint8_t run = t.attractToggle ^ 1u;
            t.attractToggle = run;
            if (run == kAttractRun) { // the attract intro (scripts 3 / 4)
                setRun(EntryMode::NewGame, kAttractStage, 0, 0, 0, kNormalDifficulty);
                mainTask().switchMode(MainMode::ToGame);
                taskSwitch(TaskEntry::Main);
                return;
            }
            if (run == kDemoRun) { // the demo (ST51 sub 1)
                setRun(EntryMode::Demo, kDemoStage, kDemoRoom, kDemoStory, kDemoStory, kNormalDifficulty);
                mainTask().switchMode(MainMode::ToDemo);
                taskSwitch(TaskEntry::Main);
                return;
            }
        }
    }
    if (pad0().pressed & kPadConfirm) {
        soundPlayFlat(UiSound::Confirm);
        const int8_t busy = fadeState().busy;
        if (busy < 1 && (busy != 0 || fadeState().covered != 0)) {
            t.enter(TitleState::Menu);
            t.clearArgs();
            return;
        }
        t.enter(TitleState::Menu, uint8_t(TitleMenuStep::Music));
        t.clearArgs();
    }
}
// exe 0x429430 (the menu row, 4 rows) / exe 0x4295b0 (the difficulty row, difficultyLastRow() + 1 rows): up / down move
// the row (wrapping) with the cursor's slide and the sound
void cursorStep(int8_t& row, int last) {
    TitleBlock& t = titleBlock();
    const uint16_t p = pad0().pressed;
    if (t.slideTicks != 0) {
        t.slideTicks -= 1u;
        return;
    }
    if (MenuPointer& pt = menuPointer(); pt.moved && pt.row < kListCell) { // OpenDash: the mouse's row
        pt.moved = false;
        if (pt.row >= 0 && pt.row <= last && pt.row != row) { // as a d-pad move: the slide, the sound
            t.slideTicks = kCursorSlideTicks;
            t.ticks = paceTicks(kSlideTicksPerPace);
            t.previousRow = int16_t(row);
            soundPlayFlat(UiSound::Cursor);
            row = int8_t(pt.row);
        }
        return;
    }
    if (!(p & (kPadUp | kPadDown))) return;
    t.slideTicks = kCursorSlideTicks;
    t.ticks = paceTicks(kSlideTicksPerPace);
    t.previousRow = int16_t(row);
    soundPlayFlat(UiSound::Cursor);
    if (!(p & kPadUp)) {
        const int8_t c = int8_t(row + 1);
        row = c;
        if (c > last) row = 0;
    } else {
        const int8_t c = int8_t(row - 1);
        row = c;
        if (c < 0) row = int8_t(last);
    }
}
// exe 0x4291b0: the menu (new game / continue / tutorial / options)
void titleMenu() {
    TitleBlock& t = titleBlock();
    auto draws = [] {
        drawTitleBackground();
        drawMenu();
    };
    auto leave = [&t](TitleState s) { // the common tail of continue / options
        t.enter(s);
        t.clearArgs();
        fade(kWipe20Request, 0);
        soundPlayFlat(UiSound::Confirm);
    };
    switch (TitleMenuStep(t.step)) {
    case TitleMenuStep::FadeIn:
        if (fadeState().busy != 0 || (fileLoads().loading & kFileLoading)) return;
        fade(kFadeInRequest, 0);
        t.brightness = 0;
        t.step += 1;
        [[fallthrough]];
    case TitleMenuStep::Music:
        soundPlayMusicIfIdle(kTitleMusic);
        t.step += 1;
        [[fallthrough]];
    case TitleMenuStep::Brighten:
        t.brightness += kMenuBrightnessStep;
        if (t.brightness > uint16_t(kFullBrightness)) {
            t.brightness = uint16_t(kFullBrightness);
            t.step += 1;
        }
        draws();
        return;
    case TitleMenuStep::Choose: {
        const uint16_t p = pad0().pressed;
        if (!(p & (kPadCircle | kPadCross | kPadStart))) {
            cursorStep(t.menuRow, kMenuLastRow);
        } else if (p & kPadConfirm) {
            t.step = uint8_t(uint8_t(t.menuRow) + t.step + 1u);
        } else {
            t.enter(TitleState::PressEnter, uint8_t(PressEnterStep::Brighten));
            t.clearArgs();
            soundPlayFlat(UiSound::Cancel);
        }
        draws();
        return;
    }
    case TitleMenuStep::NewGame: // difficulty 1, or the difficulty choice once a game was cleared
        draws();
        soundPlayFlat(UiSound::Confirm);
        if (gameState().bestDifficulty == 0) {
            setRun(EntryMode::NewGame, kNewGameStage, 0, 0, 0, kNormalDifficulty);
            startRun();
            return;
        }
        t.enter(TitleState::Difficulty);
        t.clearArgs();
        return;
    case TitleMenuStep::Continue:
        draws();
        leave(TitleState::Continue);
        return;
    case TitleMenuStep::Tutorial: // ST4A
        draws();
        setRun(EntryMode::Tutorial, kTutorialStage, 0, 0, 0, kNormalDifficulty);
        fadeState().busy = 0;
        fade(kWipe26Request, 0);
        soundPlayFlat(UiSound::Confirm);
        mainTask().switchMode(MainMode::ToGame);
        taskSwitch(TaskEntry::Main);
        return;
    case TitleMenuStep::Options:
        draws();
        leave(TitleState::Options);
        return;
    default: return;
    }
}
// exe 0x4294b0: the difficulty choice
void titleDifficulty() {
    TitleBlock& t = titleBlock();
    const uint16_t p = pad0().pressed;
    const DifficultyStep step = DifficultyStep(t.step);
    if (step == DifficultyStep::Enter) {
        t.difficultyRow = 0;
        t.step = uint8_t(DifficultyStep::Choose);
    } else if (step == DifficultyStep::Start) {
        drawTitleBackground();
        drawDifficulty();
        soundPlayFlat(UiSound::Confirm);
        setRun(EntryMode::NewGame, kNewGameStage, 0, 0, 0, uint8_t(t.difficultyRow + 1));
        startRun();
        return;
    } else if (step != DifficultyStep::Choose) {
        return;
    }
    if (!(p & (kPadCircle | kPadCross | kPadStart))) {
        cursorStep(t.difficultyRow, int(difficultyLastRow()));
    } else if (!(p & kPadConfirm)) {
        t.enter(TitleState::Menu, uint8_t(TitleMenuStep::Brighten));
        t.clearArgs();
        soundPlayFlat(UiSound::Cancel);
    } else {
        t.step += 1;
    }
    drawTitleBackground();
    drawDifficulty();
}
// the title's class table and clear flag kept, the game's display mode for a sub-screen
void enterSubScreen() {
    TitleKeeps& k = titleKeeps();
    k.classTables = stageClassTables().animated;
    k.clearBackground = drawEnvironments().clearBackground;
    drawEnvironments().clearBackground = 1;
    setDisplayMode(kGameDisplayMode);
}
// ... given back, the title's display mode, the menu at its first step
void leaveSubScreen() {
    TitleKeeps& k = titleKeeps();
    stageClassTables().animated = k.classTables;
    drawEnvironments().clearBackground = k.clearBackground;
    setDisplayMode(kTitleDisplayMode);
    TitleBlock& t = titleBlock();
    t.enter(TitleState::Menu);
    t.clearArgs();
}
// exe 0x429650: continue (the load menu, game/save_menus)
void titleContinue() {
    TitleBlock& t = titleBlock();
    switch (ContinueStep(t.step)) {
    case ContinueStep::WaitFade:
        if (fadeState().busy == 0) t.step += 1;
        return;
    case ContinueStep::Open:
        if (display().drawBuffer == 0) return;
        enterSubScreen();
        loadMenuBlock().clearBytes(); // exe 0x5488f0
        t.step += 1;
        return;
    case ContinueStep::Menu: {
        const int r = loadMenuTick(*H->fade, H->readSave);
        loadMenuBackdropDraw(D);
        t.step = uint8_t(t.step + r);
        return;
    }
    case ContinueStep::Loaded: { // the game from the save image
        GameState& g = gameState();
        g.loadSave = 1;
        g.entryMode = uint8_t(EntryMode::NewGame);
        mainTask().switchMode(MainMode::ToGame);
        taskSwitch(TaskEntry::Main);
        return;
    }
    case ContinueStep::Cancelled:
        if (display().drawBuffer == 0) return;
        // exe 0x549a20: the display environments restored (renderer)
        leaveSubScreen();
        return;
    default: return;
    }
}
// exe 0x429750: the options -- the status sub-screen's options page
void titleOptions() {
    TitleBlock& t = titleBlock();
    switch (TitleOptionsStep(t.step)) {
    case TitleOptionsStep::WaitFade:
        if (fadeState().busy == 0) t.step += 1;
        return;
    case TitleOptionsStep::Open:
        if (display().drawBuffer == 0) return;
        enterSubScreen();
        statusScreenOpen();        // exe 0x5e6e70
        statusScreenOpenOptions(); // exe 0x5e8580
        t.step += 1;
        return;
    case TitleOptionsStep::Page:
        if (statusScreenFrame(D) != 0) t.step += 1; // exe 0x5e6f00
        return;
    case TitleOptionsStep::Close:
        if (display().drawBuffer == 0) return;
        leaveSubScreen();
        return;
    default: return;
    }
}
// exe 0x429830: game over (G_OVER00.DAT), then the title's Load
void titleGameOver() {
    TitleBlock& t = titleBlock();
    switch (GameOverStep(t.step)) {
    case GameOverStep::WaitSounds:
        if (!soundDone(kMusicFades)) return;
        datLoadRequest(kGameOverDat);
        t.step += 1;
        return;
    case GameOverStep::WaitFade:
        if (fadeState().busy != 0 || display().drawBuffer == 0) return;
        setDisplayMode(kTitleDisplayMode);
        t.step += 1;
        return;
    case GameOverStep::FadeIn:
        if (fileLoads().loading & kFileLoading) return;
        fade(kFadeInRequest, 0);
        // exe 0x5d4f30: empty
        soundPlayFlatForced(kGameOverSound);
        t.brightness = 0;
        t.step += 1;
        [[fallthrough]];
    case GameOverStep::Brighten:
        t.brightness += kTitleBrightnessStep;
        if (t.brightness > uint16_t(kFullBrightness)) {
            t.brightness = uint16_t(kFullBrightness);
            t.ticks = paceTicks(kIdleTicksPerPace);
            t.step += 1;
        }
        drawGameOver();
        return;
    case GameOverStep::Shown:
        drawGameOver();
        t.ticks -= 1u;
        if (t.ticks == 0 || (pad0().pressed & (kPadCircle | kPadCross | kPadStart))) {
            fade(kWipe20Request, 0);
            soundFadeOut(kMusicFades, kTitleMusicFadeStep, 0);
            t.enter(TitleState::Load);
            t.clearArgs();
        }
        return;
    default: return;
    }
}

// one frame of the running entry
void frame() {
    if (T->running == TaskEntry::Main) {
        MainTaskState& m = mainTask();
        switch (m.currentMode()) { // [0x8ae900]
        case MainMode::Boot: bootMode(); break;
        case MainMode::Logo: logoMode(); break;
        case MainMode::ToTitle: loadMode(kDemoDat, true, TitleState::Load, 0); break;
        case MainMode::ToDemo: loadMode(kGameDat, false, TitleState::Load, kFrontEndDemo); break;
        case MainMode::ToGame: loadMode(kGameDat, false, TitleState::Load, kFrontEndGame); break;
        case MainMode::ToGameOver: loadMode(kDemoDat, true, TitleState::GameOver, 0); break;
        default: throw std::logic_error("title port: main task mode " + std::to_string(m.mode));
        }
        return;
    }
    if (T->running == TaskEntry::Title) {
        TitleBlock& t = titleBlock();
        switch (t.currentState()) { // [0x88c4a0]
        case TitleState::Load: titleLoad(); break;
        case TitleState::PressEnter: titlePressEnter(); break;
        case TitleState::Menu: titleMenu(); break;
        case TitleState::Difficulty: titleDifficulty(); break;
        case TitleState::Continue: titleContinue(); break;
        case TitleState::Options: titleOptions(); break;
        case TitleState::GameOver: titleGameOver(); break;
        default: throw std::logic_error("title port: title state " + std::to_string(t.state));
        }
        applyActorFrees(); // exe 0x5e15f0
        return;
    }
    throw std::logic_error("title port: the game loop runs in OpenDash's PlayerView");
}

} // namespace

void gameOverRescue() {
    const GameOverRescuePlace to = content().gameOverRescuePlace();
    requestTransition(kTransitionAccepted, to.stage, to.room, to.x, to.y, to.z, to.yaw, kRescueFadeIn, kNoFade);
    loadoutRestore();
}

void setTitlePs1Layout(bool on) { gPs1Layout = on; }

void titleTaskFrame(TitleTasks& t, TitleHost& host, std::vector<ScreenPrim>* draw) {
    T = &t;
    H = &host;
    D = draw;
    frame();
    D = nullptr;
}

bool softResetFrame(FadeTask& fade) {
    GameState& g = gameState();
    const int8_t mode = int8_t(g.mode);
    if (mode < int8_t(GameMode::Field) || mode > int8_t(GameMode::SelectMenuB)) return false;
    if (storyFlag(kFlagStoryMenuOpen)) return false;
    if ((pad0().held & (kPadSelect | kPadStart)) != (kPadSelect | kPadStart)) {
        g.resetHoldTicks = 0;
        return false;
    }
    const int16_t c = int16_t(g.resetHoldTicks + display().pace);
    g.resetHoldTicks = uint16_t(c);
    if (c <= kSoftResetHold || fadeState().busy != 0) return false;
    softReset(fade);
    return true;
}

void softReset(FadeTask& fade) {
    display().pace = kGameDisplayPace;
    soundStopAll();
    windowCloseAll(WindowClose::Force); // exe 0x5577d0(1)
    padSetRepeat(kPadRepeatDelay, kPadRepeatRate);
    fadeStart(fade, kWipe2ARequest, 0);
    mainTask().enterMode(MainMode::ToTitle);
}

bool gameOverModeFrame(FadeTask& fade) {
    GameState& g = gameState();
    switch (GameOverModeStep(g.modeStep)) {
    case GameOverModeStep::CloseWindows:
        windowCloseAll(WindowClose::Force); // exe 0x5577d0(1)
        g.modeStep += 1;
        [[fallthrough]];
    case GameOverModeStep::Fade:
        if (fadeState().busy != 0) return false;
        if (!storyFlag(kFlagGameOverRescue)) {
            fadeStart(fade, kFadeOutRequest | 4, 0);
        } else {
            fadeStart(fade, kFadeOutRequest | 2, 0);
            g.modeStep += 1;
        }
        soundFadeOut(kAllFades, kGameOverMusicFadeStep, 0);
        g.modeStep += 1;
        return false;
    case GameOverModeStep::Leave:
        if (fadeState().busy != 0 || !soundDone(kAllFades)) return false;
        soundStopAll();
        mainTask().switchModeClearingArgs(MainMode::ToGameOver);
        return true;
    case GameOverModeStep::Rescue:
        if (fadeState().busy != 0) return false;
        gameOverRescue();
        g.setMode(GameMode::Field);
        g.modeStep = uint8_t(FieldStep::WaitBuffer);
        return false;
    default: return false;
    }
}

void titleTaskSwitch(TitleTasks& t, TaskEntry e) {
    t.pending = e;
    t.switched = true;
}

void titleTaskCommit(TitleTasks& t) {
    t.running = t.pending;
    t.switched = false;
}

} // namespace od::port
