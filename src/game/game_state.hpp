// The game's state the systems read: the game task's mode and its step (exe 0x5413a0's caller; game/transition runs the
// field modes, game/title the others), the frame counter, the running stage and room, the story, the play time, the
// difficulty, the records the saves keep, the two balances and their tiers, the counters the message scripts keep, the
// Flutter's dock, the best times, the options and the game loop's save area (what the demo and the status sub-screen
// keep while they run).
#pragma once

#include <cstddef>
#include <cstdint>

#include "game/state_array.hpp"

namespace od::port {

// +0: the game task's mode (its handler table 0x8ae898)
enum class GameMode : uint8_t {
    StageLoad = 1,    // the stage's files loaded (OpenDash's loader; game/transition stageLoaded ends it)
    RoomLoaded = 2,   // the room set up, the player placed (exe 0x541340)
    Field = 3,        // the field play and its transitions (exe 0x5413a0)
    Pause = 4,        // the PC pause (exe 0x5418f0; not ported)
    StatusMenu = 5,   // the status sub-screen (exe 0x541bf0)
    SelectMenuA = 6,  // the select button's prompt while story flag 0x246 is set: the exam's "give up?" (exe 0x541e10)
    SelectMenuB = 7,  // ... while flag 0x24d is set: the time trial's "quit?" (exe 0x541fd0)
    GameOver = 8,     // the player died
};
// +1 of the field mode (exe 0x5413a0)
enum class FieldStep : uint8_t {
    FadeIn = 0,          // the arrival's fade started (a running fade first ends)
    FadeInWait = 1,      // until the fade ends
    Playing = 2,         // the play: a transition request moves on (an in-stage warp at once); the field menus
    WaitBuffer = 3,      // a frame of buffer 1 (another stage: the buffer mode for the load)
    StopSounds = 4,      // another stage: the sound voices stopped
    WaitFiles = 5,       // the file loads; the request's handshake 1 -> 2 -> 3
    RunExitScript = 6,   // the frames run until the script asks for the load (request 3), then the fade out
    FadeOutWait = 7,     // until the fade ends
    Leave = 8,           // the request cleared, the script's last tick: the stage load / the room load
};
// +1 of the room loaded mode (exe 0x541340)
enum class RoomLoadedStep : uint8_t {
    PlaceAfterFade = 0, // a fade ended and a frame of buffer 1: the player placed, then the setup
    Setup = 1,          // the room set up, the camera, the field mode
};
// +0xe: the shell around the game (the main task's mode that started it)
constexpr uint8_t kFrontEndNone = 0;  // the boot
constexpr uint8_t kFrontEndTitle = 1; // the title's options / the demo's title
constexpr uint8_t kFrontEndDemo = 2;  // the attract demo (no pad vibration, no stage callback)
constexpr uint8_t kFrontEndGame = 3;  // the game proper (GAME.DAT's main task): the field menus open

// +1 of the game-over mode (exe 0x542190)
enum class GameOverModeStep : uint8_t {
    CloseWindows = 0, // the windows closed
    Fade = 1,         // no fade running: the fade out and the music's fade
    Leave = 2,        // the fade and the music done: the main task's game over
    Rescue = 3,       // the rescue flag: the content's warp, then the field
};
// +0xd: the entry mode of a new run (exe 0x573fd0's cases: the parts / weapons / items the run starts with)
enum class EntryMode : uint8_t {
    NewGame = 0,  // the title's new game
    Tutorial = 1, // the title's tutorial (ST4A)
    Kit2 = 2,     // the kits 2, 3, 5..7 (their starts are not traced)
    Kit3 = 3,
    Demo = 4,     // the idle demo (ST51 sub 1)
    Kit5 = 5,
    Kit6 = 6,
    Kit7 = 7,
};

struct GameState {
    uint16_t frame = 0;         // the frame counter (effects pulse / alternate with it)
    uint8_t mode = 0;           // the game mode (GameMode)
    uint8_t modeStep = 0;       // the mode's step (FieldStep, RoomLoadedStep, GameOverModeStep ...)
    // the bytes after the step that the exe's mode stores clear with it (the dword / block stores below; no reader is
    // traced)
    StateArray<uint8_t, 8> modeSpare{};
    uint16_t resetHoldTicks = 0; // start + select held this long (pace units): the soft reset
    uint8_t loadSave = 0;       // the game mode's init applies the save image (1) or starts a new game (0)
    uint8_t entryMode = 0;      // EntryMode of a new run
    uint8_t frontEnd = 0;       // kFrontEnd*
    uint8_t stageClass = 0;     // the stage's byte of the exe table 0x88c368 (its reader is not traced)
    uint8_t stage = 0;          // the running stage
    uint8_t room = 0;           // the running stage's room (sub-room)
    uint8_t flutterDock = 0;    // the island the Flutter is docked at (game/travel)
    uint8_t playerKind = 0;     // the player's kind (copied into the player's type: 0 Megaman)
    uint8_t story = 0;          // the story's progress
    uint8_t nextStory = 0;      // the story's progress at the next arrival (the arrival moves on to it)
    uint8_t difficulty = 0;
    uint8_t savesExist = 0;     // the save scan found a slot (the title's cursor starts on continue)
    // the records the saves carry across (a load keeps the better: the smaller time, the larger others)
    uint32_t bestTime = 0;       // the best clear time (frames; -1: none)
    uint16_t bestDifficulty = 0; // the hardest difficulty cleared (the title offers the difficulties)
    uint16_t clears = 0;         // the games cleared
    uint32_t playTime = 0;       // the play time (frames, capped; the save's time)
    uint32_t playTimeCopy = 0;   // a second play-time count beside it in the save (its reader is not traced)
    int32_t zenny = 0;           // the money (game/rewards zennyAdd: at most 9999999)
    uint32_t unreadCounter = 0;  // a dword beside it in the save (its reader is not traced)
    uint32_t damageTaken = 0;    // the last damage the player took (exe 0x5e5320) ...
    uint32_t damageShown = 0;    // ... and its copy the HUD counts down (hypothesis)
    uint32_t defeats = 0;        // the enemies defeated (exe 0x5e53e0 counts both) ...
    uint32_t defeatsTotal = 0;   // ... (hypothesis: one of the two is reset by the stage)
    int16_t balance = 0;         // the good / bad balance (+-0x7fff; the player's colour follows it)
    int16_t toolKitBalance = 0;  // the second balance (the gifts sold in the shop raise it; the dev room's prices
                                 // follow its tier)
    int8_t balanceTier = 0;      // BalanceTier of each balance (the shop's prices follow the first)
    int8_t toolKitTier = 0;
    int8_t enemyNear = 0;        // an enemy is near (the hit pass's lock-on selection: 0 none, 1 near; the HUD's radar
                                 // colour)
    // counters the message scripts test (fb 47), each advanced (unless 0 or 0xff) when the Flutter docks at another
    // island
    StateArray<uint8_t, 8> dockCounters{};
    uint8_t countedDock = 0;     // the dock the counters last advanced at
    // the entries collected (ST05's entry list, content/yosyonke/st05_entry_list): message ids & 0x7f, bit 7 not read
    // yet, 0xff = none; the save keeps them and a new game sets them as eight dwords (entryLogWord)
    StateArray<uint8_t, 0x20> entryLog{};
    uint32_t clearTime = 0;      // the game clear's play time (script 0x76 records it, the clear-data save keeps it)
    // the counters the message scripts add to and test (fb 37 / fb 42): [0] the donations (the island's houses are
    // repaired from them), [1] a second one (its meaning is not traced)
    StateArray<uint16_t, 2> scriptCounters{};

    GameMode gameMode() const { return GameMode(mode); }
    // the bytes from the room to the next story (+0x11..+0x15) cleared one at a time, in order (a run's start)
    void clearRoomToNextStory() {
        room = 0;
        flutterDock = 0;
        playerKind = 0;
        story = 0;
        nextStory = 0;
    }

    // the frame counter as the exe reads it as a dword (its readers keep only the counter's low bits)
    uint32_t frameWord() const { return frame; }
    // the dock counters as the save copies them (two dwords, little-endian)
    uint32_t dockCountersWord(size_t k) const { return packWord(dockCounters, k); }
    void setDockCountersWord(size_t k, uint32_t v) { unpackWord(dockCounters, k, v); }
    // the entry log as the save copies it and a new game sets it (eight dwords, little-endian)
    static constexpr size_t kEntryLogWords = 8;
    uint32_t entryLogWord(size_t k) const { return packWord(entryLog, k); }
    void setEntryLogWord(size_t k, uint32_t v) { unpackWord(entryLog, k, v); }
    // the mode block cleared (the mode, its step, the spare bytes, the soft reset's hold: the main task's hand-over to
    // the game loop)
    void clearModeBlock() {
        mode = 0;
        modeStep = 0;
        modeSpare.fill(0);
        resetHoldTicks = 0;
    }
    FieldStep fieldStep() const { return FieldStep(modeStep); }
    // the mode m (byte store, the step kept)
    void setMode(GameMode m) { mode = uint8_t(m); }
    // the mode m at step 0 (the exe's word store)
    void switchMode(GameMode m) {
        mode = uint8_t(m);
        modeStep = 0;
    }
    // the mode m at step 0, the two spare bytes after the step cleared (the exe's dword store)
    void enterMode(GameMode m) {
        switchMode(m);
        modeSpare[0] = 0;
        modeSpare[1] = 0;
    }

private:
    template <size_t N> static uint32_t packWord(const StateArray<uint8_t, N>& b, size_t k) {
        return uint32_t(b[k * 4]) | uint32_t(b[k * 4 + 1]) << 8 | uint32_t(b[k * 4 + 2]) << 16 |
               uint32_t(b[k * 4 + 3]) << 24;
    }
    template <size_t N> static void unpackWord(StateArray<uint8_t, N>& b, size_t k, uint32_t v) {
        for (size_t i = 0; i < 4; ++i) b[k * 4 + i] = uint8_t(v >> (8 * i));
    }
};
// the hardest difficulty (the weapons' attack halved, the pickups' smallest multiplier)
constexpr uint8_t kHardestDifficulty = 4;
inline GameState& gameState() {
    static GameState s;
    return s;
}
// A balance's tier (+-0x7fff in three bands, with a hysteresis: exe 0x5e51d0 / 0x5e5260)
enum class BalanceTier : int8_t {
    High = 0,   // above 0x4000 (back to Middle at 0x2fff and below)
    Middle = 1,
    Low = 2,    // below -0x4000 (back to Middle at -0x2fff and above)
};

// A best time: the three-letter name -- glyph words, ended by kBestTimeNameEnd -- and the time: the race courses'
// three rows each (content/late_game/st59: course * 3 + row, sorted by the time), the records fb 48 / 4a / 4b name
// and print
struct BestTime {
    StateArray<uint16_t, 4> name{}; // three letters and kBestTimeNameEnd
    int32_t time = 0;
};
constexpr uint16_t kBestTimeNameEnd = 0xff;
constexpr size_t kBestTimes = 9;
constexpr size_t kBestTimeWords = kBestTimes * 3;
struct BestTimes {
    StateArray<BestTime, kBestTimes> records{};

    // the records as the save copies them (kBestTimeWords dwords: per record the name's four words as two dwords,
    // then the time)
    uint32_t word(size_t i) const {
        const BestTime& r = records[i / 3];
        const size_t w = i % 3;
        if (w == 2) return uint32_t(r.time);
        return uint32_t(r.name[w * 2]) | uint32_t(r.name[w * 2 + 1]) << 16;
    }
    void setWord(size_t i, uint32_t v) {
        BestTime& r = records[i / 3];
        const size_t w = i % 3;
        if (w == 2) {
            r.time = int32_t(v);
            return;
        }
        r.name[w * 2] = uint16_t(v);
        r.name[w * 2 + 1] = uint16_t(v >> 16);
    }
};
inline BestTimes& bestTimes() {
    static BestTimes s;
    return s;
}

// The options (the status sub-screen's settings page)
struct GameOptions {
    uint8_t mono = 0;             // the sound in mono (no 3D pan)
    uint8_t minimapShown = 0;     // the minimap shown (game/minimap; the map page switches it): set by a new game,
                                  // restored by a load
    uint8_t controlOption = 0;    // cleared by the defaults, kept by the demo (its reader is not traced)
    uint8_t controllerLayout = 0; // the controller row's value
    uint8_t lookDirection = 0;    // the camera stick's look direction (0: the stick reversed)
    uint8_t vibrationOff = 0;
    uint8_t autoLockSpecial = 0;  // the special weapon's trigger locks on (needs the lock reached)
    uint8_t autoLockBuster = 0;   // the buster's
    uint8_t ruby = 0;             // the ruby (furigana) shown over the glyphs (the lines with ruby are taller)

    // the controller bytes controlOption / controllerLayout cleared (the exe's word store: the boot)
    void clearControllerWord() {
        controlOption = 0;
        controllerLayout = 0;
    }
};
inline GameOptions& gameOptions() {
    static GameOptions s;
    return s;
}

// The game loop's save area: what the demo keeps of the pad and the options while it replays, and what the status
// sub-screen keeps of the field while it runs. The two never run together: the sub-screen keeps the background flag
// as a dword that covers the demo's three option bytes after it (keepClearBackground clears them). The field's class
// table it saves is game/actors' StageClassTables.
struct GameLoopSaves {
    uint16_t demoPadType = 0;
    uint32_t statusOpenBits = 0;       // MessageSystem::openBits
    uint8_t statusClearBackground = 0; // DrawEnvironments::clearBackground
    uint8_t demoControllerLayout = 0;
    uint8_t demoControlOption = 0;
    uint8_t demoAutoLockSpecial = 0;
    uint32_t statusWaitBits = 0;       // MessageSystem::waitBits
    uint8_t demoLookDirection = 0;
    uint8_t demoAutoLockBuster = 0;

    // the background flag as the sub-screen keeps it (the dword store: the demo's three bytes after it cleared)
    void keepClearBackground(uint8_t v) {
        statusClearBackground = v;
        demoControllerLayout = 0;
        demoControlOption = 0;
        demoAutoLockSpecial = 0;
    }
    // ... and as it reads it back (the dword: the demo's bytes above the flag's)
    uint32_t keptClearBackground() const {
        return uint32_t(statusClearBackground) | uint32_t(demoControllerLayout) << 8 |
               uint32_t(demoControlOption) << 16 | uint32_t(demoAutoLockSpecial) << 24;
    }
};
inline GameLoopSaves& gameLoopSaves() {
    static GameLoopSaves s;
    return s;
}

} // namespace od::port
