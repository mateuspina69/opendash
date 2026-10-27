// The stage / room transitions (see transition.hpp).
#include "game/transition.hpp"

#include "engine/data_source.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_sweeps.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/display.hpp"
#include "game/encounter.hpp"
#include "game/event_state.hpp"
#include "game/fade_state.hpp"
#include "game/game_state.hpp"
#include "game/hud.hpp"
#include "game/message.hpp"
#include "game/event.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_update.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/minimap.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/room_scripts.hpp"
#include "game/visited_rooms.hpp"
#include "game/stage_files.hpp"
#include "game/stage_state.hpp"
#include "game/story_flags.hpp"
#include "game/transition_state.hpp"
#include "game/travel.hpp"

namespace od::port {

namespace {

constexpr const char* kTransitionArea = "transition port"; // the area of its port-gap errors

// The running game task frame's context (set by each field mode tick; the script fades and the frame body use it)
const PlayerEnv* gEnv = nullptr;
TransitionHooks* gHooks = nullptr;
FadeTask* gFade = nullptr;

// story flags the field mode and the room scripts test
constexpr uint32_t kShopFlag = 0x6ff;           // the shop's clerk asked for the shop (script 8)
constexpr uint32_t kRestoreOnLeaveFlag = 0x6f1; // leaving the room restores the player
constexpr uint32_t kInputBackOnLeaveFlag = 0x247; // leaving the room gives the input back
constexpr uint32_t kNoFieldMenusFlag = 0x245, kFieldMenusHeldFlag = 0x6f2; // the select button opens no menu
constexpr uint32_t kSelectMenuAFlag = 0x246, kStatusMenuOffFlag = 0x24d;   // which menu it opens
constexpr uint8_t kExitTransitionScript = 0, kShopScript = 8;
constexpr uint8_t kStageLoadStep = 2;          // the stage load's step the field mode leaves to (exe 0x5411c0)
constexpr uint32_t kPlayTimeCap = 21599999;    // 99:59:59 at 60 frames a second
// the in-stage warp's classes: all but the player and the sky layers (no script) / the kept ones (a script runs)
constexpr uint32_t kWarpClasses = 0xffc, kWarpClassesInScript = 0x7b4;

// The exe's table of the stage load: a byte per stage
const DataTable<ScalarTable<uint8_t, 0x60>> stageClasses{TableId::TransitionStageClasses};

// The stage's room handlers of the stages whose room scripts test the door by sub-room (exe tables by room
// 0x8cab00 / 0x8cab50 / 0x8cd690 / 0x8cd7b8; the named routines of engine/routines.csv)
using RoomHandlers = DataTable<RoutineTable<0x20>>;
const RoomHandlers st2dRoomHandlers{TableId::St2dRoomHandlers};
const RoomHandlers st2eRoomHandlers{TableId::St2eRoomHandlers};
const RoomHandlers st4eRoomHandlers{TableId::St4eRoomHandlers};
const RoomHandlers st51RoomHandlers{TableId::St51RoomHandlers};
constexpr uint8_t kST26DoorStory = 0xd, kST29ShopStory = 0xc;

// exe 0x512890: the room scripts. Only the door / exit part is ported: flag 0x700 (set by the trigger actions exe
// 0x413310 and the door actors) runs script 0 for the stages whose case tests it. The per-stage room handlers (tables
// 0x8c5024 .., story x sub-room) are the host's hook (game/spawn; NPC / event logic).
void roomScripts() {
    if (scriptState().inUse()) return;
    const int st = gameState().stage, story = gameState().story;
    const uint8_t room = gameState().room;
    bool door;
    switch (st) {
    case 0x26: case 0x27: case 0x28: case 0x33: case 0x34: door = story > kST26DoorStory; break;
    // ST3D / ST3F: the exe tests the block again (always clear here: the scripts returned above); kept as written
    case 0x3d: case 0x3f: door = !scriptState().inUse(); break;
    case 4: case 7: case 8: case 9: case 0xa: case 0xc: case 0xd: case 0xe: case 0xf: // (5 / 6 / 0xb / 0x15 /
    // 0x16: their cases test the door themselves, content/yosyonke/town_room_cases / content/nino/st15_st16_rooms)
    case 0x10: case 0x11: case 0x14: case 0x17: case 0x18: case 0x19: case 0x1a: case 0x1b:
    case 0x1d: case 0x1e: case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x29: case 0x2b:
    case 0x2c: case 0x32: case 0x35: case 0x36: case 0x37: case 0x38: case 0x3b: case 0x3c: case 0x3e: case 0x40:
    case 0x46: case 0x47: case 0x48: case 0x4a: case 0x4d: case 0x54: case 0x55: case 0x56:
        door = true;
        break;
    case 0x2d: case 0x2e: { // the per-room handlers: the ones that only run script 0 on flag 0x700 (exe 0x582490) or
        // test it first (exe 0x584a60, ST2E room 4); the others are not ported
        const RoutineId handler = (st == 0x2d ? st2dRoomHandlers[room] : st2eRoomHandlers[room]).id();
        door = handler == RoutineId::RoomDoorScript || handler == RoutineId::St2eRoom4Handler;
        break;
    }
    case 0x4e: case 0x51: { // the ruin under Manda (ST24 room 0 exit 2): the per-room handlers only. Every one of them
        // ends with the unconditional flag-0x700 -> exe 0x511270(0) test (exe 0x582490; 0x58b900 ST4E room 4, 0x58bf80
        // / 0x58c010 ST51 rooms 1 / 2: disassembly 2026-10-09): the lifts (door actors exe 0x59ce50, kind 14) of ST4E/0
        // (up to ST24), ST4E/9 (down to ST51) and ST51/0 need it
        const RoutineId handler = (st == 0x4e ? st4eRoomHandlers[room] : st51RoomHandlers[room]).id();
        door = handler == RoutineId::RoomDoorScript || handler == RoutineId::St4eRoom4Handler ||
               handler == RoutineId::St51Room1Handler || handler == RoutineId::St51Room2Handler;
        break;
    }
    default: door = false; break;
    }
    // the stages whose case is only the handler call keep their door test inside the handler (e.g. ST12 / ST13: exe
    // 0x57e380 / 0x57e5c0 / 0x57e630 / 0x57ea80 .. end with flag 0x700 -> exe 0x511270(0)): a handler the port lacks
    // still gets that test, else a door never starts the transition script (the arrival walk-in included)
    if (!door && roomScriptHandlerMissing()) door = true;
    if (gHooks && gHooks->roomHandler) gHooks->roomHandler();
    if (door && storyFlag(kFlagTransitionArmed)) {
        scriptRun(kExitTransitionScript);
        return;
    }
    // the shop stages: the clerk's message sets flag 0x6ff -> exe 0x5112b0(8) = the stage callback *0xa6bb80 (the shop
    // menu exe 0x5d1e30, game/shop); ST29 only after story 0xc (evidence: the cases 0x57cce0, 0x580040, 0x5800a0,
    // 0x581b10, 0x583730)
    const bool shopStage =
        st == 0x0a || st == 0x1a || st == 0x1b || st == 0x25 || (st == 0x29 && story > kST29ShopStory);
    if (shopStage && storyFlag(kShopFlag)) scriptStart(kShopScript);
}

// exe 0x5d5b30(param): the room (re)set up after a room change (param < 0: an in-stage switch; else the actors are
// killed first and, unless a script runs into a room load, the player restarts), the minimap's reset (exe 0x415480), the
// map marker, the minimap on while the stage has a callback, the hit list emptied (exe 0x5e4110)
void roomInit(int param) {
    if (param < 0) {
        if (gHooks->roomSetup) gHooks->roomSetup(param);
        // exe 0x415520: the map marker (below)
    } else {
        killAllActors();
        if (gHooks->roomSetup) gHooks->roomSetup(param);
        Player& p = player();
        if (param != 0 || !scriptState().inUse()) {
            p.enterState(PlayerState::Init);
            p.inputMode &= uint8_t(~kInputOff);
            playerInputAndDispatch(*gEnv);
        }
        minimapRoomReset(p); // exe 0x415480 (+ the map marker below)
    }
    mapMarkerRefresh();
    if (stage().roomCallback != RoutineId::None) minimapSwitchOn();
    if (gameState().frontEnd == kFrontEndDemo) stage().roomCallback = RoutineId::None;
    actorSystem().hitCount = 0;
}

// exe 0x542280: the frame body (the script runner, the host's systems, the script block's idle step)
void frameBody() {
    if (gHooks->frameStart) gHooks->frameStart();
    scriptRunnerTick(*gEnv);
    if (gHooks->frame) gHooks->frame();
    scriptBlockIdle();
}

// the story moved on to its next value (the story flag group cleared first) when they differ
void advanceStory() {
    GameState& g = gameState();
    if (g.story != g.nextStory) {
        clearStoryFlagGroup(kFlagGroupStory);
        g.story = g.nextStory;
    }
}

// the field menus (the select button in play): the status sub-screen, or the menus of the flags
void openFieldMenus() {
    GameState& g = gameState();
    const Player& p = player();
    if (g.gameMode() == GameMode::Field && g.fieldStep() == FieldStep::Playing && g.frontEnd == kFrontEndGame &&
        transitionRequest().state == kTransitionNone && !scriptState().inUse() && fadeState().busy == 0 &&
        p.hitPoints >= 0 && !storyFlag(kNoFieldMenusFlag) && (pad0().pressed & kPadSelect) &&
        eventState().active == 0 && !(p.inputMode & kInputOff) && !storyFlag(kFieldMenusHeldFlag)) {
        if (storyFlag(kSelectMenuAFlag)) g.enterMode(GameMode::SelectMenuA);
        else if (!storyFlag(kStatusMenuOffFlag)) {
            g.enterMode(GameMode::StatusMenu);
            soundLoopState().holdLoops = 1;
        } else {
            g.enterMode(GameMode::SelectMenuB);
        }
    }
}

// the in-stage warp (request kTransitionWarp): every kept actor and the player moved by the exit's offset (or, while a
// script runs, the others freed), the player at the destination, the room switched in place
void inStageWarp() {
    TransitionRequest& r = transitionRequest();
    Player& p = player();
    if (!scriptState().inUse()) {
        int16_t offset[3] = {int16_t(r.destination.x - p.pos.x.whole), 0, int16_t(r.destination.z - p.pos.z.whole)};
        actorsSweep(kSweepSkyLayers, kSweepFree);
        actorsWarp(kWarpClasses, offset);
        if (gHooks->warp) gHooks->warp(offset[0], offset[2]);
        p.lockAimPoint.x = int16_t(p.lockAimPoint.x + offset[0]);
        p.lockAimPoint.z = int16_t(p.lockAimPoint.z + offset[2]);
        p.screenAimPoint.x = int16_t(p.screenAimPoint.x + offset[0]);
        p.screenAimPoint.z = int16_t(p.screenAimPoint.z + offset[2]);
    } else {
        actorsWarp(kWarpClassesInScript, nullptr);
    }
    encounterDropFreedTracked();
    p.prevPos.x.whole = r.destination.x;
    p.pos.x.whole = r.destination.x;
    p.prevPos.y.whole = r.destination.y;
    p.pos.y.whole = r.destination.y;
    p.prevPos.z.whole = r.destination.z;
    p.pos.z.whole = r.destination.z;
    StageState& s = stage();
    s.warpOffsetX = int16_t(s.warpOffsetX - r.trigger.x);
    s.warpOffsetZ = int16_t(s.warpOffsetZ - r.trigger.z);
    gameState().room = r.destRoom;
    advanceStory();
    roomInit(-1);
    breakableWallsReset();
    r.state = kTransitionNone;
}

} // namespace

void playTimeTick() {
    GameState& g = gameState();
    for (uint32_t* counter : {&g.playTime, &g.playTimeCopy}) {
        uint32_t v = *counter + display().pace;
        if (int32_t(v) > int32_t(kPlayTimeCap)) v = kPlayTimeCap;
        *counter = v;
    }
}

void scriptFade(uint32_t request, uint8_t param) {
    if (!gFade) notPorted(kTransitionArea, "a script fade outside the game task frame");
    fadeStart(*gFade, request, param);
}

void arrivalPlacement() {
    const TransitionRequest& r = transitionRequest();
    if (r.keepPosition != 0) return;
    Player& p = player();
    const int32_t x = int32_t(uint32_t(int32_t(r.destination.x)) << 16);
    const int32_t y = int32_t(uint32_t(int32_t(r.destination.y)) << 16);
    const int32_t z = int32_t(uint32_t(int32_t(r.destination.z)) << 16);
    p.prevPos.x = x;
    p.pos.x = x;
    p.prevPos.y = y;
    p.pos.y = y;
    p.prevPos.z = z;
    p.pos.z = z;
    const int16_t yaw = r.destination.yaw;
    p.wasAirborne = 0;
    p.prevRotation.y = yaw;
    p.rotation.y = yaw;
    p.airborne = 0;
    // the status markers are gone with the room: their bits off (kStatusArrived when there were any)
    const uint16_t status = p.status;
    if (status & kStatusMarkerMask) p.status = uint16_t((status & kStatusEffectMask) | kStatusArrived);
    else p.status = uint16_t(status & kStatusEffectMask);
    GameState& g = gameState();
    g.stage = r.destStage;
    g.room = r.destRoom;
    advanceStory();
}

void fieldMode(const PlayerEnv& env, TransitionHooks& hooks, FadeTask& fade) {
    gEnv = &env;
    gHooks = &hooks;
    gFade = &fade;
    rngStep();
    playerTickReset();
    eventTick(); // exe 0x543680
    roomScripts();
    if (hooks.roomScripts) hooks.roomScripts();
    display().frontEndLayer = 0; // exe 0x549a70(0)
    GameState& g = gameState();
    TransitionRequest& r = transitionRequest();
    const FadeState& f = fadeState();
    const uint8_t step = g.modeStep;
    auto render = [&]() { // exe 0x5422f0: the draw pass (nothing while the frame draws nothing): the host's, then
        if (hooks.render) hooks.render(); // at the pass's entry, before its drawingOff test (the host checks it too)
        if (display().drawingOff != 0) return;
        minimapDrawPass(); // exe 0x4155b0 (after the sprite banner 0x5f2690)
    };
    auto run = [&]() { // exe 0x542280; 0x5422f0; 0x5426e0
        frameBody();
        render();
        playTimeTick();
    };
    auto next = [&]() { // the step + 1, then the frame
        ++g.modeStep;
        run();
    };
    switch (FieldStep(step)) {
    case FieldStep::FadeIn:
        if (f.busy != 0) return run();
        if (r.fadeIn == kNoFade) {
            g.modeStep = uint8_t(step + 2);
            return run();
        }
        fadeStart(fade, r.fadeIn, 0);
        return next();
    case FieldStep::FadeInWait:
        if (f.busy != 0) return run();
        g.modeStep = uint8_t(step + 1);
        return run();
    case FieldStep::Playing: {
        const uint8_t request = r.state;
        if (request != kTransitionNone) {
            if (request == kTransitionWarp) inStageWarp();
            else g.modeStep = uint8_t(step + 1);
        }
        frameBody();
        render();
        openFieldMenus();
        playTimeTick();
        return;
    }
    case FieldStep::WaitBuffer:
        if (display().drawBuffer == 0) return run();
        if (g.stage != r.destStage) setBufferMode(1);
        return next();
    case FieldStep::StopSounds:
        if (g.stage != r.destStage) {
            // exe 0x5d5a00: the SFX voices 0x10..0x16 but 0x12 stopped, the loop slots cleared
            soundStopSfxVoicesKeep18();
            // exe 0x5d5be0(destination stage): the file request (OpenDash loads in mode 1)
        }
        return next();
    case FieldStep::WaitFiles:
        if (fileLoads().loading & 1) return run();
        switch (r.state) {
        case kTransitionAsked: r.state = kTransitionAccepted; return next();
        case kTransitionAccepted:
            r.state = kTransitionScriptDone;
            g.modeStep = uint8_t(g.modeStep + 2);
            return run();
        case kTransitionScriptDone: g.modeStep = uint8_t(step + 1); return run();
        default: return run();
        }
    case FieldStep::RunExitScript:
        frameBody();
        render();
        if (r.state == kTransitionScriptDone) {
            const uint8_t fadeOut = r.fadeOut;
            if (fadeOut == kNoFade) { // the sub-room switched in place
                r.state = kTransitionNone;
                g.modeStep = uint8_t(FieldStep::Playing);
                encounterSlotsRearm();
                if (g.room == r.destRoom) {
                    arrivalPlacement();
                } else {
                    arrivalPlacement();
                    scriptRunnerTick(env);
                    scriptBlockIdle();
                    roomInit(1);
                    breakableWallsReset();
                }
            } else {
                g.modeStep = uint8_t(g.modeStep + (fadeOut < kWipe20Request ? 1u : 2u));
                fadeStart(fade, fadeOut, 0);
            }
        }
        playTimeTick();
        return;
    case FieldStep::FadeOutWait:
        if (f.busy != 0) return run();
        encounterSlotsRearm();
        ++g.modeStep;
        [[fallthrough]];
    case FieldStep::Leave:
        r.state = kTransitionNone;
        scriptRunnerTick(env);
        scriptBlockIdle();
        if (storyFlag(kRestoreOnLeaveFlag)) playerRestore(); // exe 0x542430
        if (storyFlag(kInputBackOnLeaveFlag)) {
            Player& p = player();
            p.inputMode = 0;
            p.invulnerable = 0;
            p.flags |= kActorLiveDrawn;
        }
        if (g.stage != r.destStage) {
            g.setMode(GameMode::StageLoad);
            g.modeStep = kStageLoadStep;
        } else {
            g.setMode(GameMode::RoomLoaded);
            g.modeStep = uint8_t(RoomLoadedStep::PlaceAfterFade);
        }
        playTimeTick();
        return;
    default: playTimeTick(); return;
    }
}

void roomLoadedMode(const PlayerEnv& env, TransitionHooks& hooks, FadeTask& fade) {
    gEnv = &env;
    gHooks = &hooks;
    gFade = &fade;
    const RoomLoadedStep step = RoomLoadedStep(gameState().modeStep);
    if (step == RoomLoadedStep::PlaceAfterFade) {
        if (fadeState().busy != 0 || display().drawBuffer == 0) return;
        arrivalPlacement();
        setBufferMode(0);
    } else if (step != RoomLoadedStep::Setup) {
        return;
    }
    roomInit(0);
    if (hooks.camera) hooks.camera();
    breakableWallsReset();
    hudBanner().hide(); // exe 0x5f2680
    eventState().reset();       // exe 0x543660
    gameState().switchMode(GameMode::Field);
}

void stageLoaded() {
    arrivalPlacement(); // substate 2
    // substate 3: a game entered from the title (640 x 480, pace 1 = 60 / s) gets the game's display mode back (exe
    // 0x549880(0), call at 0x54126f): mode 0, pace 2 (30 / s) -- without it the whole game ran at twice the speed after
    // the title
    if (display().mode != kGameDisplayMode) setDisplayMode(kGameDisplayMode);
    // substate 4: exe 0x4153e0 (the HUD's elements, exe 0x415f40), exe 0x542460 (the demo only), exe 0x5d5a80 (the
    // stage's bytes, exe 0x5d5ae0; exe 0x5f5960: the room block's pointers = OpenDash's loader, the two stage callbacks
    // cleared; exe 0x415410: the minimap and the map's marker cleared; the per-stage setup = OpenDash's loader, after
    // this; exe 0x5127a0(0)), exe 0x549860(0), the room loaded mode
    hudElementsInit();
    GameState& g = gameState();
    const uint8_t st = g.stage;
    g.stageClass = stageClasses[st];
    flutterDockUpdate(); // exe 0x5d5a80's dock and exe 0x5d5ae0
    stage().secondRoomCallback = RoutineId::None; // exe 0x5f5960
    stage().roomCallback = RoutineId::None;
    minimapStageReset(); // exe 0x415410
    mapMarkerStageReset();
    // exe 0x5127a0(0): the stage's flag groups, the room script's state, the script block
    clearStoryFlagGroup(kFlagGroupStage);
    clearStoryFlagGroup(kFlagGroupRoom);
    RoomScriptState& room = roomScriptState();
    room.clearHandlerWord();
    for (size_t i = 0; i < room.actors.size(); ++i) room.actors[i] = 0;
    room.actorMask = 0;
    scriptState().clearForStageLoad();
    setBufferMode(0);
    g.setMode(GameMode::RoomLoaded);
    g.modeStep = uint8_t(RoomLoadedStep::Setup);
}

void gameTick(const PlayerEnv& env, TransitionHooks& hooks, FadeTask& fade) {
    switch (GameMode(gameState().mode)) {
    case GameMode::Field: fieldMode(env, hooks, fade); break;
    case GameMode::RoomLoaded: roomLoadedMode(env, hooks, fade); break;
    default: notPortedAt(kTransitionArea, "game mode", gameState().mode);
    }
    applyActorFrees();
}

} // namespace od::port
