// The boot / title flow of dash2.exe outside the game loop: the main task (exe 0x542ff0, its modes [0x8ae900 +
// MainMode * 4]: the boot, the Capcom logo, the loads that hand over to the title task or to the game loop) and the
// title task (exe 0x428ed0, its states [0x88c4a0 + TitleState * 4]: TITLE.DAT, "press enter" and the attract timer,
// the menu, the difficulty choice, continue (the load menu, game/save_menus), the options (the status sub-screen's
// options page, game/status_screen), the game-over picture); also the soft reset and the game-over mode of the game loop
// that switch back to them. Bit-exact against tests/data/title_*.bin (test_title). Notes:
//
// The two tasks never run with the game loop (exe 0x541070, OpenDash's PlayerView): a task switch (exe 0x604b06)
// replaces the running entry, which starts on the next frame. The title runs at pace 1 (60 ticks / s), the game loop
// at pace 2 (30 / s).
//
// Draw: the packet builders (the 640 x 480 display of the title's display mode) are reproduced as ScreenPrim lists for
// OpenDash's 2D pass.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "engine/screen_prim.hpp"
#include "game/fade.hpp"
#include "game/state_array.hpp"

namespace od::port {

// ---- the main task's block (0xa7a000) -------------------------------------------------------------------------------
// +0: the mode (the handler table 0x8ae900)
enum class MainMode : uint8_t {
    Boot = 0,       // exe 0x543010: INIT.DAT, the boot's defaults
    Logo = 1,       // exe 0x543120: the Capcom logo, the save scan, then the attract (ToGame in ST02)
    ToTitle = 2,    // exe 0x5432b0: DEMO.DAT, then the title task's first state
    ToDemo = 3,     // exe 0x543350: GAME.DAT, then the game loop as the attract demo
    ToGame = 4,     // exe 0x5433f0: GAME.DAT, then the game loop as the game
    ToGameOver = 5, // exe 0x543490: DEMO.DAT, then the title task's game-over picture
};
// +1 of the logo mode
enum class LogoStep : uint8_t {
    DisplayMode = 0, // on a frame of buffer 1: the title's display mode
    LoadLogo = 1,    // LOGO.DAT queued
    FadeIn = 2,      // the load and the fade done: the fade in from black
    Brighten = 3,    // the logo's brightness up to full, then the hold
    ScanSaves = 4,   // the save slots scanned
    Advance5 = 5,    // the steps 5 / 7 move on by one, 6 by two (the scan's timer set again)
    RescanTimer = 6,
    Advance7 = 7,
    Shown = 8,       // the hold counts down (circle / start ends it): the attract
};
// +1 of the modes that load a DAT
enum class MainLoadStep : uint8_t {
    WaitBuffer = 0, // on a frame of buffer 1: the buffer mode for the load
    RequestDat = 1,
    WaitDat = 2,    // the load done: the hand-over
};

struct MainTaskState {
    uint8_t mode = 0;        // MainMode
    uint8_t step = 0;        // the mode's step (LogoStep, MainLoadStep)
    uint16_t args = 0;       // the mode's argument word (a script's game over passes 1)
    int16_t brightness = 0;  // the logo's brightness (0..0x80)
    int16_t logoTicks = 0;   // the logo's hold (counted down past the scan)
    uint16_t scanTicks = 0;  // set by the scan steps (its reader is not traced)
    StateArray<uint8_t, 6> spare{}; // the block's last bytes (cleared with it; no reader is traced)

    MainMode currentMode() const { return MainMode(mode); }
    // the block cleared (a host's start of the main task)
    void clearBlockBytes() { *this = MainTaskState{}; }
    // the mode and its step as one word (the host compares it to tell a mode change)
    uint16_t modeAndStep() const { return uint16_t(mode | step << 8); }

    // the mode m at step 0 (the exe's word store at +0)
    void switchMode(MainMode m) {
        mode = uint8_t(m);
        step = 0;
    }
    // ... and the word after it cleared (the exe's two word stores: the game-over mode's hand-over)
    void switchModeClearingArgs(MainMode m) {
        switchMode(m);
        args = 0;
    }
    // the mode m at step 0, the argument word cleared (the exe's dword store: the soft reset)
    void enterMode(MainMode m) { switchModeClearingArgs(m); }
    // the mode m at step 0 with the argument word `a` (the exe's dword store: a script's game over passes 1)
    void enterMode(MainMode m, uint16_t a) {
        switchMode(m);
        args = a;
    }
};
inline MainTaskState& mainTask() {
    static MainTaskState s;
    return s;
}

// ---- the title task's block (0xa6bf80) ------------------------------------------------------------------------------
// +0: the state (the handler table 0x88c4a0)
enum class TitleState : uint8_t {
    Load = 0,       // exe 0x428f00: TITLE.DAT, then PressEnter (or Menu when +2 asks for it)
    PressEnter = 1, // exe 0x428fe0: "PRESS ENTER"; idle, the attract (ST02) / the demo (ST51) alternately
    Menu = 2,       // exe 0x4291b0: new game / continue / tutorial / options
    Difficulty = 3, // exe 0x4294b0: the difficulty choice (once a game was cleared)
    Continue = 4,   // exe 0x429650: the load menu (game/save_menus)
    Options = 5,    // exe 0x429750: the status sub-screen's options page
    GameOver = 6,   // exe 0x429830: G_OVER00.DAT's picture, then Load
};
// +1 of Load
enum class TitleLoadStep : uint8_t { WaitSounds = 0, RequestDat = 1, SetUp = 2 };
// +1 of PressEnter
enum class PressEnterStep : uint8_t { FadeIn = 0, Brighten = 1, Idle = 2 };
// +1 of Menu (Choose + 1 + the row chosen: the rows' own steps)
enum class TitleMenuStep : uint8_t {
    FadeIn = 0, Music = 1, Brighten = 2, Choose = 3, NewGame = 4, Continue = 5, Tutorial = 6, Options = 7,
};
// +1 of Difficulty
enum class DifficultyStep : uint8_t { Enter = 0, Choose = 1, Start = 2 };
// +1 of Continue
enum class ContinueStep : uint8_t { WaitFade = 0, Open = 1, Menu = 2, Loaded = 3, Cancelled = 4 };
// +1 of Options
enum class TitleOptionsStep : uint8_t { WaitFade = 0, Open = 1, Page = 2, Close = 3 };
// +1 of GameOver
enum class GameOverStep : uint8_t { WaitSounds = 0, WaitFade = 1, FadeIn = 2, Brighten = 3, Shown = 4 };

struct TitleBlock {
    uint8_t state = 0;          // TitleState
    uint8_t step = 0;           // the state's step
    uint8_t straightToMenu = 0; // Load goes to the menu instead of "press enter"
    uint8_t argHigh = 0;        // the byte after it (cleared with it; no reader is traced)
    uint16_t ticks = 0;         // the idle countdown / the menu cursor's slide (pace units)
    uint16_t ticksB = 0;        // cleared by Load (its reader is not traced)
    StateArray<uint8_t, 4> restartSpare{}; // the third word a restart clears (no reader is traced)
    int8_t menuRow = 0;         // the menu's cursor row
    int8_t difficultyRow = 0;
    uint16_t brightness = 0;    // the pictures' brightness (0..0x80)
    uint8_t attractToggle = 0;  // the idle's alternation: 0 the attract, 1 the demo
    uint8_t slideTicks = 0;     // the cursor's slide between two rows (counted down)
    int16_t previousRow = 0;    // the row the cursor slides from
    StateArray<uint8_t, 0xc> unread14{}; // +0x14..+0x1f, up to the controller pool: no code writes them (only the
                                         // rider walk past the static pool reads them, game/carry)

    TitleState currentState() const { return TitleState(state); }
    // the state s at its step `step` (the exe's word store at +0)
    void enter(TitleState s, uint8_t stepIndex = 0) {
        state = uint8_t(s);
        step = stepIndex;
    }
    // +2 / +3 cleared (the exe's word store)
    void clearArgs() {
        straightToMenu = 0;
        argHigh = 0;
    }
    // the task (re)started at the state s: the block's first three dwords (the exe's stores of the main task)
    void restart(TitleState s) {
        enter(s);
        clearArgs();
        ticks = 0;
        ticksB = 0;
        restartSpare.fill(0);
    }
};
inline TitleBlock& titleBlock() {
    static TitleBlock s;
    return s;
}

// The entries of the game task
enum class TaskEntry : uint8_t { Main, Title, GameLoop };

struct TitleHost {
    FadeTask* fade = nullptr; // the fade task (task 2) of the app
    // exe 0x548910: the DASH2-%02d.DAT slots that exist (bit n = slot n); the port writes the slot bits
    std::function<uint32_t()> scanSaves;
    // exe 0x548de0 (slot, buffer, size) for the load menu (game/save_menus loadMenuTick): -1 = failed
    std::function<int(uint32_t slot, uint8_t* out, uint32_t size)> readSave;
};

struct TitleTasks {
    TaskEntry running = TaskEntry::Main; // the entry of the game task now
    TaskEntry pending = TaskEntry::Main; // exe 0x604b06: the entry from the next frame on
    bool switched = false;               // pending != running (consumed by the caller at the frame end)
};

// ---- the game loop's side ------------------------------------------------------------------------------------------
// exe 0x5410b0 (after the mode handler, modes 3..7): start + select held for more than kSoftResetHold pace units (no
// story menu open, no fade): the soft reset below. Returns true when it switched.
bool softResetFrame(FadeTask& fade);
// the reset itself (exe 0x5410b0 once the hold has run): pace 2, sounds stopped, windows closed, the pads' repeat, fade
// 0x2a, the main task to ToTitle (dword store); also OpenDash's "back to the title" row of the options page
void softReset(FadeTask& fade);
// the start + select hold that resets (pace units)
constexpr int16_t kSoftResetHold = 0x78;

// exe 0x542190, the game loop's mode 8 (entered when the player dies): windows closed, fade 0x14 (0x12 and step 3 with
// the rescue flag), the music fades out; then (fade and sounds done) the sounds stop, the main task to ToGameOver and the
// task switches to it -> the title's game-over picture. Step 3 (the rescue flag): the content's rescue warp, then the
// field mode. The exe also runs the field frame body in steps 0..2: the caller's business. Returns true when it
// switched.
bool gameOverModeFrame(FadeTask& fade);

// One frame of the task when it is not the game loop: the main task's mode or one title-task state (+ exe 0x5e15f0,
// the pending actor frees). A switch to another entry takes effect on the next frame. `draw` receives the primitives
// the frame's packet builders link (nullptr: none).
void titleTaskFrame(TitleTasks& t, TitleHost& host, std::vector<ScreenPrim>* draw);
// A language pack's title art is in VRAM: the logo, the copyright and the difficulty rows are drawn with the PS1
// overlay's geometry
void setTitlePs1Layout(bool on);
// exe 0x604b06 from the game loop's side: a script / game over wrote the main task's mode and switched to it
void titleTaskSwitch(TitleTasks& t, TaskEntry e);
// called by the caller after the frame: running = pending
void titleTaskCommit(TitleTasks& t);

} // namespace od::port
