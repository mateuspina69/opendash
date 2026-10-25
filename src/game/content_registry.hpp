// The extension points of the game systems (src/game) that the per-area content (src/content) fills in: the actor
// classes, the story scripts and room-script cases, the room handlers of the stage setup, and a few single calls of
// the systems into content code. The game code never names a content function: it dispatches through this
// registry. od::content::registerAll() (content/registry.cpp) fills it once, before the first tick, in the host (app
// main, each test); until then content() throws, so a host that forgot it fails loudly instead of running with
// every content class "unported".
//
// The chains (vectors) run in registration order and the first entry that handles a call wins: content/registry.cpp
// keeps the order the dispatch had when it was a hand-written chain of calls.
#pragma once

#include <array>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <utility>
#include <vector>

#include "engine/routine_ids.hpp"
#include "game/actors.hpp"
#include "game/actor_movement.hpp"
#include "game/effects_draw.hpp"

namespace od::port {

// a story script module (the script's handler is [0x89b6d0 + id * 4]): whether it ports the script, and its tick
struct ScriptModule {
    bool (*ported)(uint8_t id);
    bool (*tick)(const PlayerEnv& env, uint8_t id);
};

// where a game over rescues the player to (exe 0x5428b0: the transition's stage, sub-room, place and yaw)
struct GameOverRescuePlace {
    uint8_t stage, room;
    int16_t x, y, z, yaw;
};

// a room handler (game/room_scripts) / room helper (game/stage_setup) of a stage, by its routine: run = false only
// answers whether fn is transcribed (RoutineId::None, a pointer with no C++ routine, is never)
using RoomHandlerFn = bool (*)(RoutineId fn, bool run);

// A stage's room function [0x8d397c + stage * 4] beyond the template (game/stage_setup roomSetupActors runs the parts
// at their places; a stage that has any part has an entry, its code beyond the template transcribed)
struct RoomFunctionParts {
    void (*afterEncounterSlots)() = nullptr; // after the encounter slots' room (exe 0x50f320)
    void (*weaponMode)() = nullptr;          // instead of the table's constant weapon mode (exe 0x573570's call)
    void (*tail)() = nullptr;                // after the weapon mode
};

struct ContentRegistry {
    // ---- actors (game/actors registry()): the class's update routine (engine/routines.csv) -> its port; a routine
    // registered twice throws
    std::map<RoutineId, ActorUpdateFn> classes;
    void addClass(RoutineId cls, ActorUpdateFn fn);
    void addClasses(std::initializer_list<std::pair<const RoutineId, ActorUpdateFn>> entries);

    // ---- story (game/story)
    // storyScriptPorted / storyScriptTick, past the game's own scripts
    std::vector<ScriptModule> scripts;
    std::vector<bool (*)()> roomCases;                   // storyRoomCase: the stage cases of exe 0x512890
    // the stages roomCases covers (game/room_scripts roomScriptCovered)
    std::vector<bool (*)(uint8_t stage)> roomCasesKnown;

    // ---- stage / room setup (game/stage_setup) and room scripts (game/room_scripts)
    std::vector<RoomHandlerFn> roomHandlers;   // room-script handlers (the stage cases' and the rooms')
    std::vector<RoomHandlerFn> roomHelpers;    // the stages' room helpers, past the template's
    // by stage: the encounter kinds its stage setup computes (exe 0x50f280's argument, called right after the ground
    // kinds' write; -1: no call)
    std::map<uint8_t, int32_t (*)()> stageEncounterKinds;
    std::map<uint8_t, RoomFunctionParts> roomFunctions; // by stage: the room function beyond the template
    std::map<uint8_t, void (*)()> roomCaseHeads;    // by stage: run before the stage case's handler
    std::map<uint8_t, void (*)()> roomCaseTails;    // by stage: run after the stage case's handler
    std::map<uint8_t, bool (*)()> stageCases;       // by stage: the whole room-script stage case
    struct {
        bool (*known)() = nullptr;
        bool (*run)() = nullptr;
    } subruinRoomScripts; // the sub-ruins ST4D..ST50 (their room scripts depend on the RNG's encounter layout)

    // by routine: the stage callback script 0x30 runs (StageState::script30Callback; game/story)
    std::map<RoutineId, void (*)()> script30Callbacks;
    // by routine: the minimap's stage callback the draw pass runs (StageState::roomCallback; game/minimap)
    std::map<RoutineId, void (*)()> roomCallbacks;

    // ---- the effects' draw (game/effects_draw): the content classes' draws by class routine (after the game's, in the
    // pools' slot order), the texture keys they sample (each adder appends to the up-front list, after the game's, in
    // registration order) and their gradient rows (after the game's: fx::contentGradientRow finds one)
    std::map<RoutineId, fx::ClassDraw> effectDraws;
    std::vector<void (*)(std::vector<fx::Texture>& out)> effectTextures;
    std::vector<std::array<uint32_t, 2>> effectGradientRows;

    // ---- single calls of the systems
    // the running scripts that draw the white screen this frame (exe 0x5f30e0's callers; game/hud asks each)
    std::vector<bool (*)()> scriptWhiteScreens;
    // the place a game over with the rescue flag warps to (exe 0x5428b0's places: game/title gameOverRescue)
    GameOverRescuePlace (*gameOverRescuePlace)() = nullptr;

    bool ready = false; // set by od::content::registerAll once everything is in
};

// The registry to fill (content/registry.cpp only)
ContentRegistry& contentRegistry();
// The filled registry; throws std::logic_error before od::content::registerAll() ran
const ContentRegistry& content();

} // namespace od::port
