// ST54's game-clear screen, script 0x76 (exe 0x53ef40). Names are hypotheses from the observed behaviour. Render
// output: the shadow list exe 0x5f20f0 is left out (as the other cast ports). The screen sprites of script 0x76 (exe
// 0x53f2f0, the backdrop exe 0x5e7fe0(1)) are collected for the app (clearScreenPrims).
//
// Script 0x76 (ST54, case 0x58c160 on flag 0x681): the game-clear screen -- the clear time (hh:mm:ss of the play
// time), the records (the best time, the hardest difficulty, the clears), then (step 1, 0x548f20) the clear-data save
// menu, then the main task's mode ToTitle (the task switch to exe 0x542ff0).
#include "content/late_game/st54_clear_screen.hpp"
#include "content/late_game/st54_clear_screen_state.hpp"

#include <algorithm>

#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_sweeps.hpp"
#include "game/display.hpp"
#include "game/fade_state.hpp"
#include "game/game_state.hpp"
#include "game/menu_draw.hpp"
#include "game/message.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/save_menus.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"
#include "game/title.hpp"
#include "game/transition.hpp"

using namespace od::port::st54_clear_screen_state;

namespace od::port {

namespace {

// the script's state routines (exe table 0x8ae7b8, by the state) and its steps' routines
const DataTable<RoutineTable<3>> clearScreenStates{TableId::St54ClearScreenStates};
constexpr RoutineId kShowTime = RoutineId::St54ClearScreenShowTime, kSaveMenu = RoutineId::St54ClearScreenSaveMenu;
enum class Opening : uint8_t { Start = 0, HidePlayer = 1, WaitFade = 2 };
enum class Closing : uint8_t { Start = 0, WaitFade = 1 };
enum class ShowTimePhase : uint8_t { FadeIn = 0, Shown = 1 };
constexpr uint8_t kFadeIn = 2, kFadeOut = 0x12;
constexpr uint32_t kGrey = 0x808080;
// the sprites of the table 0x8ae720: the digits 0..9, the colon, the "please wait" (0xb), the labels 0xc..0xe
constexpr uint8_t kColonSprite = 10, kWaitSprite = 0xb, kTimeLabels = 0xc;
constexpr int16_t kTimeRow = 0x68;
constexpr uint32_t kSweepAll = 0xffe;
constexpr uint8_t kSweepFree = 0x80;
constexpr uint32_t kFramesPerSecond = 0x3c;

ClearScreenFields& fields() {
    return scriptState().part<ClearScreenFields>();
}
// eight bytes the opening clears (their reader is not traced)

// One sprite of the table 0x8ae720 (exe data, 10 bytes)
struct ScreenSpriteRecord : DataRecord {
    static constexpr uint32_t kBytes = 0x9;
    int16_t x = 0; // +0x0
    int16_t y = 0; // +0x2
    uint8_t u = 0; // +0x4
    uint8_t v = 0; // +0x5
    uint8_t height = 0; // +0x6
    uint8_t width = 0; // +0x7
    uint8_t flags = 0; // +0x8: kSpriteSemi | kSpriteAdditive
    template <class R> static ScreenSpriteRecord load(const R& r) {
        ScreenSpriteRecord v;
        v.x = r.template read<int16_t>(0x0);
        v.y = r.template read<int16_t>(0x2);
        v.u = r.template read<uint8_t>(0x4);
        v.v = r.template read<uint8_t>(0x5);
        v.height = r.template read<uint8_t>(0x6);
        v.width = r.template read<uint8_t>(0x7);
        v.flags = r.template read<uint8_t>(0x8);
        return v;
    }
};
constexpr uint8_t kSpriteSemi = 0x01, kSpriteAdditive = 0x02;
ScreenSpriteRecord screenSprite(uint8_t i) { return dataRecord<ScreenSpriteRecord>(stateAddress(TableId::St54ClearScreenSprites) + uint32_t(i) * 10, ScreenSpriteRecord::kBytes); }

// the steps runner exe 0x53f090 (no skip): the step's routine or, at the list's end, the wrap-up; the sequence tick
// (inlined 0x5114d0)
template <class F> void runSteps(F step) {
    ScriptState& s = scriptState();
    const ScriptStep current = scriptStepAt(s.steps.value());
    if (current.id != kScriptStepEnd) {
        if (const RoutineRef routine = stateRoutine(current.routine)) step(routine);
    } else {
        s.enterState(CutsceneState::WrapUp);
    }
    scriptSequenceTick();
}

// ---- screen sprites ------------------------------------------------------------------------------------------------
struct Chain {
    uint32_t slot; // the OT entry (offset from 0xa67840)
    std::vector<ScreenPrim> p;
};
std::vector<Chain> gChains;
std::vector<ScreenPrim> gPrims;
ScreenPrim sprite(int x, int y, int w, int h, uint32_t uvc, uint32_t col, uint8_t code, uint16_t tpage) {
    ScreenPrim p;
    p.kind = ScreenPrim::Sprite;
    p.x = int16_t(x);
    p.y = int16_t(y);
    p.w = int16_t(w);
    p.h = int16_t(h);
    p.u = uint8_t(uvc);
    p.v = uint8_t(uvc >> 8);
    p.clut = uint16_t(uvc >> 16);
    p.colour = col & 0xffffff;
    p.semi = (code & 2) != 0;
    p.raw = (code & 1) != 0;
    p.tpage = tpage;
    return p;
}
// exe 0x53f2f0(i, colour, slot, xy): one sprite of the table 0x8ae720 (draw mode 0xe100023a (abr 1) when additive,
// else 0xe100021a) into OT entry 0x70 + slot * 4; xy = -1: the record's position
void screenSpriteDraw(uint8_t i, uint32_t col, uint8_t slot, bool at, int16_t x, int16_t y) {
    const ScreenSpriteRecord r = screenSprite(i);
    const uint8_t f = r.flags;
    const uint16_t mode = (f & kSpriteAdditive) ? 0x23a : 0x21a;
    if (!at) {
        x = r.x;
        y = r.y;
    }
    gChains.push_back({0x70u + uint32_t(slot) * 4,
                       {sprite(x, y, r.width, r.height, 0x3e080000u | uint32_t(r.v) << 8 | r.u, col,
                               (f & kSpriteSemi) ? 0x66 : 0x64, uint16_t(mode & 0x1ff))}});
}
// exe 0x5e7fe0(switching): the scrolling backdrop, 21 x 16 tiles of 16 px (draw mode 0xd, clut 0x7f92), OT entry 0x88
// (0x78 while switching); OpenDash widescreen: the drawing area and the tile columns continued past the 320 frame
// (game/menu_draw backdropDraw is the same packets on the menus' chains)
void backdropDraw(int switching) {
    uint8_t t = uint8_t(gameState().frame);
    if (display().pace == 1) t >>= 1;
    Chain c{switching < 1 ? 0x88u : 0x78u, {}};
    const int extra = screenExtra();
    if (extra > 0) {
        ScreenPrim clip;
        clip.kind = ScreenPrim::Clip;
        clip.x = int16_t(-extra);
        clip.y = 0;
        clip.w = int16_t(320 + 2 * extra);
        clip.h = 240;
        c.p.push_back(clip);
    }
    const int ph = ((t & 0x3f) >> 4) * 0xf, start = int(t & 0xf) - 0x10;
    const int more = (extra + 15) / 16;
    for (int row = 0; row < 0x10; ++row)
        for (int col = -more; col < 0x15 + more; ++col)
            c.p.push_back(sprite(start + col * 0x10, start + row * 0x10,
                                 0x7f920000u | uint32_t(((ph + row) & 3) * 0x10 + 0x20) << 8 |
                                     uint32_t(((ph + col) & 3) * 0x10 + 0x30),
                                 0x10, 0x10, kGrey, 0x7c, 0x0d));
    gChains.push_back(std::move(c));
}
void flushChains() {
    std::vector<uint32_t> slots;
    for (const Chain& c : gChains)
        if (std::find(slots.begin(), slots.end(), c.slot) == slots.end()) slots.push_back(c.slot);
    std::sort(slots.rbegin(), slots.rend());
    for (const uint32_t s : slots)
        for (size_t i = gChains.size(); i-- > 0;)
            if (gChains[i].slot == s) gPrims.insert(gPrims.end(), gChains[i].p.begin(), gChains[i].p.end());
    gChains.clear();
}

// ---- script 0x76 (states 0x8ae7b8, steps 0x8ae708) + the backdrop every tick ----------------------------------------
// The opening: vibration off, the actors swept, the sequence, the pad replay, unskippable, the clear time = the play
// time (frames) -> hours / minutes / seconds, the records (the best time, the hardest difficulty, the clears + 1); then
// the player hidden and the fade wait (the "please wait" sprite in entry 0x88). Step 0x53f130: fade 2 once, the labels
// and the time digits, start / circle -> next. Step 0x548f20: the clear-data save menu. The closing: the windows closed,
// fade 0x12; then the player shown, the pad back, the block ended, the main task's mode ToTitle (and exe
// 0x604b06(0x542ff0): the app's title flow reacts to the mode).

// exe 0x53f130: the clear time shown
void showTime() {
    ScriptState& s = scriptState();
    if (ShowTimePhase(s.stepPhase) == ShowTimePhase::FadeIn) {
        scriptFade(kFadeIn, 0);
        s.stepPhase += 1u;
    } else if (ShowTimePhase(s.stepPhase) != ShowTimePhase::Shown) {
        return;
    }
    screenSpriteDraw(kTimeLabels, kGrey, 2, false, 0, 0);
    screenSpriteDraw(kTimeLabels + 1, kGrey, 2, false, 0, 0);
    screenSpriteDraw(kTimeLabels + 2, kGrey, 2, false, 0, 0);
    const uint8_t hh = fields().hours, mm = fields().minutes, ss = fields().seconds;
    screenSpriteDraw(uint8_t(hh / 10), kGrey, 2, true, 0x48, kTimeRow);
    screenSpriteDraw(uint8_t(hh % 10), kGrey, 2, true, 0x60, kTimeRow);
    screenSpriteDraw(kColonSprite, kGrey, 2, true, 0x78, kTimeRow);
    screenSpriteDraw(uint8_t(mm / 10), kGrey, 2, true, 0x88, kTimeRow);
    screenSpriteDraw(uint8_t(mm % 10), kGrey, 2, true, 0xa0, kTimeRow);
    screenSpriteDraw(kColonSprite, kGrey, 2, true, 0xb8, kTimeRow);
    screenSpriteDraw(uint8_t(ss / 10), kGrey, 2, true, 200, kTimeRow);
    screenSpriteDraw(uint8_t(ss % 10), kGrey, 2, true, 0xe0, kTimeRow);
    if (fadeState().busy == 0 && (pad0().pressed & kPadConfirm)) scriptSequenceSkip(1);
}
void clearScreenStep(const RoutineRef& routine) {
    switch (routine.id()) {
    case kShowTime: showTime(); return;
    case kSaveMenu: // the clear-data save menu (game/save_menus clearSaveMenuTick); closed -> the next step
        if (clearSaveMenuTick()) scriptSequenceSkip(1); // exe 0x5115c0(1)
        return;
    default: notPortedAt(kStoryPort, "script 0x76 step", routine.pointerValue());
    }
}
// the clear time recorded and the records kept
void recordClear() {
    GameState& g = gameState();
    st54ClearScreenGlobals().clearedBytes = {};
    const uint32_t time = g.playTime;
    g.clearTime = time;
    uint32_t v = time / kFramesPerSecond;
    fields().seconds = uint8_t(v % 0x3c);
    v /= 0x3c;
    fields().minutes = uint8_t(v % 0x3c);
    fields().hours = uint8_t(v / 0x3c);
    if (g.bestTime > g.clearTime) g.bestTime = g.clearTime;
    if (g.bestDifficulty < g.difficulty) g.bestDifficulty = g.difficulty;
    g.clears += 1u;
}
void opening() {
    ScriptState& s = scriptState();
    switch (Opening(s.subState)) {
    case Opening::Start:
        padVibrate(0, 0x1000000);
        actorsSweep(kSweepAll, kSweepFree);
        scriptSequenceStart(stateAddress(TableId::St54ClearScreenCameraScript), scriptStepList<TableId::St54ClearScreenStepList>);
        scriptReplayPadOn();
        scriptSequenceTick();
        s.flags |= kScriptUnskippable;
        recordClear();
        s.subState += 1u;
        return;
    case Opening::HidePlayer:
        player().flags &= uint8_t(~kActorLiveDrawn);
        s.subState += 1u;
        [[fallthrough]];
    case Opening::WaitFade:
        if (fadeState().busy != 0) {
            screenSpriteDraw(kWaitSprite, kGrey, 6, false, 0, 0);
            return;
        }
        s.enterState(CutsceneState::Steps);
        return;
    default: return;
    }
}
void closing() {
    ScriptState& s = scriptState();
    if (Closing(s.subState) == Closing::Start) {
        windowCloseAll(WindowClose::Force);
        scriptFade(kFadeOut, 0);
        s.subState += 1u;
    } else if (Closing(s.subState) != Closing::WaitFade) {
        return;
    }
    if (fadeState().busy != 0) return;
    player().flags |= kActorLiveDrawn;
    playerBackToPlay(); // exe 0x5723c0
    scriptReplayPadOff();
    scriptEndBlock();
    // exe 0x604b06(0x542ff0): the game task ends here (the app's title flow reacts to the main task's mode)
    mainTask().enterMode(MainMode::ToTitle);
}
void clearScreenScript() {
    switch (clearScreenStates->idAt(scriptState().state)) {
    case RoutineId::ClearScreenOpening: opening(); return;
    case RoutineId::ScriptSequenceState: runSteps(clearScreenStep); return;
    case RoutineId::ClearScreenClosing: closing(); return;
    default: notPortedAt(kStoryPort, "script 0x76 state", scriptState().state);
    }
}

} // namespace

bool clearScreenScriptPorted(uint8_t id) {
    switch (storyScriptHandler(id).id()) {
    case RoutineId::ClearScreenScript: // script 0x76 (its save menu step: game/save_menus)
        return true;
    default: return false;
    }
}

bool clearScreenScriptTick(const PlayerEnv&, uint8_t id) {
    gChains.clear();
    gPrims.clear();
    switch (storyScriptHandler(id).id()) {
    case RoutineId::ClearScreenScript:
        clearScreenScript();
        backdropDraw(1); // exe 0x5e7fe0(1)
        flushChains();
        return true;
    default: return false;
    }
}

const std::vector<ScreenPrim>& clearScreenPrims() {
    static const std::vector<ScreenPrim> kNone;
    // only while the block runs script 0x76 (its last tick ends the block: the flags = kScriptEnded)
    const ScriptState& s = scriptState();
    if (!(s.flags & kScriptRunning) || storyScriptHandler(s.id).id() != RoutineId::ClearScreenScript) return kNone;
    return gPrims;
}

} // namespace od::port
