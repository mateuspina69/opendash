// Tron Bonne at Pokte Village: scripts 0x1b / 0x1c and their cast (see st25_tron_scenes.hpp). The scripts' states are
// their exe tables' ([0x8a301c] 0x51f0e0 / 0x51f200 / 0x51f270; [0x8a3414] 0x520070 / the shared runner 0x51f200 /
// 0x5201b0); their steps (lists 0x8a2ec0 / 0x8a3320) are dispatched by exe address. Step ticks are the scenes'
// timeline; the cast's model-bank animation indices are keyed by it.
#include "content/manda/st25_tron_scenes.hpp"
#include "content/manda/st25_tron_scenes_state.hpp"

#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/camera.hpp"
#include "game/cutscene_actor.hpp"
#include "game/display.hpp"
#include "game/explosions.hpp"
#include "game/fade_state.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/room_scripts.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/skid_sparks.hpp"
#include "game/stage_files.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"
#include "game/transition.hpp"
#include "game/transition_state.hpp"

using namespace od::port::st25_tron_scenes_state;

namespace od::port {

using namespace st25_tron_scenes_state;

namespace {

constexpr const char* kSt25Area = "island story port"; // the area of its port-gap errors

constexpr uint8_t kSt25 = 0x25;

PokteScriptBytes& bytes() {
    return scriptState().part<PokteScriptBytes>();
}
ScriptState& block() { return scriptState(); }
int stepTick() { return block().stepTick; }
uint8_t phase() { return block().stepPhase; }

// the talking poses
constexpr uint8_t kPoseStill = 0, kPoseNod = 2, kPoseThree = 3, kPoseFour = 4, kPoseFive = 5;

void sweepActors() { actorsSweep(kSweepActors, kSweepFree); }
void showPlayer(bool on) { player().setDrawn(on); }
void lockSkip(bool on) {
    if (on) block().flags |= kScriptUnskippable;
    else block().flags &= uint8_t(~kScriptUnskippable);
}
KeyframeTracks& faces() { return block().faceTracks; }
void selectEyes(uint8_t seq) { faces().first.select(seq, 0); }
void selectMouth(uint8_t seq) { faces().second.select(seq, 0); }
void stopEyes(uint8_t v) { faceTrackStop(faces(), FaceTrack::Eyes, v); }   // exe 0x5e35f0
void stopMouth(uint8_t v) { faceTrackStop(faces(), FaceTrack::Mouth, v); } // exe 0x5e3610
// the player's 16.16 position (whole units, no fraction: the exe's dword stores) and yaw
void placePlayer(int16_t x, int16_t y, int16_t z, int16_t yaw) {
    Player& p = player();
    p.pos.x = int32_t(uint32_t(uint16_t(x)) << 16);
    p.pos.y = int32_t(uint32_t(uint16_t(y)) << 16);
    p.pos.z = int32_t(uint32_t(uint16_t(z)) << 16);
    p.rotation.y = yaw;
}
void placeActor(Actor& a, int16_t x, int16_t y, int16_t z, int16_t yaw) {
    a.pos.x = int32_t(uint32_t(uint16_t(x)) << 16);
    a.pos.y = int32_t(uint32_t(uint16_t(y)) << 16);
    a.pos.z = int32_t(uint32_t(uint16_t(z)) << 16);
    a.rotation.y = yaw;
}
Actor& scriptActor(size_t k) { return actorLayoutOf(*block().actors[k]); }
// the in-stage warp to ST25's sub-room (no fades), facing a half turn, then the next step
void warpToRoom(uint8_t room, int16_t x, int16_t y, int16_t z) {
    requestTransition(kTransitionWarp, kSt25, room, x, y, z, kHalfTurn, kNoFade, kNoFade);
    scriptSequenceSkip(1);
}
// the actor kept out of the walls (exe 0x40f570 -> 0x410960) and on the ground (exe 0x40f190)
void settle(const PlayerEnv& env, Actor& a) {
    if (actorTestMove(env, a) != 0) actorPushOut(env, a);
    a.pos.y.whole = int16_t(actorGround(env, a));
}
// the player walks forward at the walk-in speed (exe 0x571910), settled
void walkIn(const PlayerEnv& env) {
    moveInFacing(player(), 0, 0, int16_t(-bytes().walkSpeed));
    settle(env, player());
}
// the player's stage animation n at a step (forced)
void stageAnim(uint8_t n, uint8_t step = 0) { playerScriptAnimId(uint8_t(kStageAnims + n), step, true); }

// the gate's two cells of the village (the gate open: variant 1; closed: 0)
void switchGateCells(uint8_t variant) {
    switchGridCellVariant(0x3f, 0x37, variant);
    switchGridCellVariant(0x3f, 0x49, variant);
}
constexpr int16_t kGateX = -0x20, kGateY = -0xe0, kGateZ = -0x3e1; // where the player waits at the gate

// ---- script 0x1b: Tron at the gate (steps 0x8a2ec0) --------------------------------------------------------------------
// 0 exe 0x51f400: at 0x363 the warp to sub 6
void stepGateOpens() {
    if (stepTick() == 0x363) warpToRoom(6, -0x100, -0x100, -0x1200);
}
// 1 exe 0x51f470: the player shown running in
void stepRunIn(const PlayerEnv& env) {
    if (stepTick() == 0) {
        showPlayer(true);
        playerScriptAnim(PlayerAnim::Run, 0, true);
        bytes().walkSpeed = 0x140;
    }
    walkIn(env);
}
// 2 exe 0x51f4f0: placed, running in, slowing to a stop
void stepSlowDown(const PlayerEnv& env) {
    switch (stepTick()) {
    case 0:
        placePlayer(-0x40, -0xe0, -0x594, kHalfTurn);
        playerScriptAnim(PlayerAnim::Run, 0, true);
        bytes().walkSpeed = 0x140;
        break;
    case 0x10: stageAnim(1); break;
    case 0x14: bytes().walkSpeed = 0x50; break;
    case 0x1b: bytes().walkSpeed = 0; break;
    default: break;
    }
    walkIn(env);
}
// 3 exe 0x51f5f0: placed at the gate; at 0x78 the warp to sub 5
void stepAtGate() {
    const int t = stepTick();
    if (t == 0) placePlayer(kGateX, kGateY, kGateZ, kHalfTurn);
    else if (t == 0x78) warpToRoom(5, kGateX, kGateY, kGateZ);
}
// the hidden steps (4 exe 0x51f690, 6 exe 0x51f870, 9 exe 0x51fba0): the player hidden, the warp to sub 6 at `ticks`
void stepHiddenUntil(int ticks, int16_t x) {
    const int t = stepTick();
    if (t == 0) showPlayer(false);
    else if (t == ticks) warpToRoom(6, x, kGateY, kGateZ);
}
// 5 exe 0x51f710: the player's stage animation 2 (its animation paused at first), the poses; at 0x120 the warp to sub 5
void stepFirstWords(const PlayerEnv& env) {
    PokteScriptBytes& s = bytes();
    switch (stepTick()) {
    case 0:
        showPlayer(true);
        settle(env, player());
        stageAnim(2);
        player().animPaused = 1;
        break;
    case 5:
        player().animPaused = 0;
        selectEyes(0);
        break;
    case 0x1d: s.talkPose = kPoseFive; break;
    case 0xf5: s.talkPose = kPoseStill; break;
    case 0x103:
        selectEyes(1);
        selectMouth(0);
        s.talkPose = kPoseFour;
        break;
    case 0x114:
        stopMouth(0x14);
        s.talkPose = kPoseStill;
        break;
    case 0x120: warpToRoom(5, kGateX, kGateY, kGateZ); break;
    default: break;
    }
}
// 7 exe 0x51f8f0: the stage animations 2 (step 10) and 3, the poses
void stepSecondWords(const PlayerEnv& env) {
    PokteScriptBytes& s = bytes();
    switch (stepTick()) {
    case 0:
        showPlayer(true);
        stageAnim(2, 10);
        settle(env, player());
        break;
    case 10:
        stageAnim(3);
        s.talkPose = kPoseFour;
        break;
    case 0x2f: s.talkPose = kPoseStill; break;
    case 0x3e: s.talkPose = kPoseFive; break;
    case 0x16f: s.talkPose = kPoseStill; break;
    default: break;
    }
}
// 8 exe 0x51f990: the long talk (the stage animations 4 / 5, the mouth's keys); at 0x239 the warp to sub 5
void stepLongTalk() {
    PokteScriptBytes& s = bytes();
    switch (stepTick()) {
    case 0: stopMouth(0x14); return;
    case 10:
        stageAnim(4);
        selectEyes(2);
        selectMouth(2);
        s.talkPose = kPoseFour;
        return;
    case 0x10: stopMouth(0x1a); return;
    case 0x19: selectMouth(0); return;
    case 0x31:
        stopMouth(0x14);
        s.talkPose = kPoseStill;
        return;
    case 0x45: s.talkPose = kPoseFive; return;
    case 0x110: s.talkPose = kPoseStill; return;
    case 0x123: s.talkPose = kPoseFive; return;
    case 0x1ee: s.talkPose = kPoseStill; return;
    case 0x1f8:
        stageAnim(5);
        selectEyes(3);
        selectMouth(1);
        s.talkPose = kPoseFour;
        return;
    case 0x237:
        stopMouth(0x17);
        s.talkPose = kPoseStill;
        return;
    case 0x239: warpToRoom(5, kGateX, kGateY, kGateZ); return;
    default: return;
    }
}
// 10 exe 0x51fc20: the stage animations 5 (step 0x13) and 6, the faces
void stepAnswer(const PlayerEnv& env) {
    PokteScriptBytes& s = bytes();
    switch (stepTick()) {
    case 0:
        showPlayer(true);
        stageAnim(5, 0x13);
        settle(env, player());
        return;
    case 10: s.talkPose = kPoseNod; return;
    case 0x6e: s.talkPose = kPoseStill; return;
    case 0x78:
        stageAnim(6);
        stopEyes(0x1c);
        selectMouth(0);
        s.talkPose = kPoseFour;
        return;
    case 0x9c:
        stopMouth(0x14);
        s.talkPose = kPoseStill;
        return;
    default: return;
    }
}
// 11 exe 0x51fda0: the stage animation 7, the poses
void stepListen() {
    PokteScriptBytes& s = bytes();
    const int t = stepTick();
    if (t > 0xec) {
        if (t == 0xf6) {
            stageAnim(7);
            s.talkPose = kPoseFour;
        } else if (t == 0x119) {
            s.talkPose = kPoseStill;
        }
        return;
    }
    if (t == 0xec || t == 0x41) s.talkPose = kPoseStill;
    else if (t == 5 || t == 0x4b) s.talkPose = kPoseNod;
}
// 12 exe 0x51fdf0: hidden, the poses
void stepTronTalks() {
    PokteScriptBytes& s = bytes();
    const int t = stepTick();
    if (t == 0x73 || t == 0x155) s.talkPose = kPoseStill;
    else if (t == 0) showPlayer(false);
    else if (t == 5 || t == 0x7d) s.talkPose = kPoseFive;
}
// 13 exe 0x51fe30: the stage animation 8 (paused at first), the faces; at 0xaa the warp to sub 5
void stepLastWords() {
    PokteScriptBytes& s = bytes();
    switch (stepTick()) {
    case 0:
        showPlayer(true);
        stageAnim(8);
        stopMouth(0x1d);
        player().animPaused = 1;
        return;
    case 5: s.talkPose = kPoseNod; return;
    case 0x73: s.talkPose = kPoseStill; return;
    case 0x7d:
        player().animPaused = 0;
        stopEyes(0x1c);
        selectMouth(3);
        s.talkPose = kPoseFour;
        return;
    case 0x91: stopMouth(0x1f); return;
    case 0x9e:
        stopMouth(0x1d);
        s.talkPose = kPoseStill;
        return;
    case 0xaa: warpToRoom(5, kGateX, kGateY, kGateZ); return;
    default: return;
    }
}
// 14 exe 0x520020: hidden; at 0x5a the skip's wipe and the next step (the end)
void stepEnd() {
    const int t = stepTick();
    if (t == 0) {
        showPlayer(false);
    } else if (t == 0x5a) {
        scriptFade(kScriptSkipWipe, 0);
        scriptSequenceSkip(1);
    }
}

// ---- script 0x1c: after the tank (steps 0x8a3320) ----------------------------------------------------------------------
constexpr size_t kWreckSlot = 0, kFigureSlot = 1, kTronSlot = 2;
// 0 exe 0x5203a0: the actors swept, the next type-1 bank walked, the player placed, the wreck with the tank's arms
void stepWreck() {
    if (stepTick() != 0) return;
    sweepActors();
    stage().bankBuffer ^= 1u;
    stageBankWalk(); // exe 0x5f3200
    placePlayer(0, -0xe0, -0x600, kHalfTurn);
    scriptActorSpawnRecord(stateAddress(TableId::St25TronScenesWreckRecord)); // exe 0x511740
    (*block().actors[kWreckSlot]).params[0] = bytes().brokenArms;
    lockSkip(false);
}
// 1 exe 0x520420: at 0x33 the player runs, turns to 0x530 a tick, stops; at 0x97 it walks on
void stepTurnAndWalk() {
    const int t = stepTick();
    ScriptState& s = block();
    if (t == 0) {
        s.clearStepPhase();
        bytes().walkSpeed = 0;
    }
    switch (s.stepPhase) {
    case 0:
        if (t != 0x33) return;
        playerScriptAnim(PlayerAnim::Run, 0, true);
        ++s.stepPhase;
        return;
    case 1: {
        const int16_t y = int16_t(uint16_t(uint16_t(player().rotation.y) - 0x30u));
        player().rotation.y = y;
        if (y < 0x530) return;
        player().rotation.y = 0x530;
        ++s.stepPhase;
        return;
    }
    case 2:
        playerScriptAnim(PlayerAnim::Idle, 0, true);
        ++s.stepPhase;
        return;
    case 3:
        if (t == 0x97) {
            playerScriptAnim(PlayerAnim::Run, 0, true);
            bytes().walkSpeed = 0x147;
        }
        moveInFacing(player(), 0, 0, int16_t(-bytes().walkSpeed));
        return;
    default: return;
    }
}
// 2 exe 0x5204e0: the player walks in (Run step 0xd, the stage animation 1), the figure placed, the faces and poses
void stepWalkToTron() {
    PokteScriptBytes& s = bytes();
    switch (stepTick()) {
    case 0: {
        block().clearStepPhase();
        playerScriptAnim(PlayerAnim::Run, 0xd, true);
        s.walkSpeed = 0x147;
        placePlayer(-0x19e, -0xe0, -0x530, 0x530);
        placeActor(scriptActor(kFigureSlot), -0x666, -0x126, -0x2a6, 0x950);
        break;
    }
    case 0x13:
        stageAnim(1);
        selectEyes(0);
        stopMouth(0x15);
        break;
    case 0x17: s.walkSpeed = 0x46; break;
    case 0x25: s.walkSpeed = 0; break;
    case 0x2f:
        selectMouth(0);
        s.talkPose = kPoseFour;
        break;
    case 0x35:
    case 0x57: stopMouth(0x15); break;
    case 0x4b:
    case 0x6e: selectMouth(0); break;
    case 0x0f:
    case 0x8c:
    case 0x136: s.talkPose = kPoseNod; break;
    case 0x7d:
        stopMouth(0x15);
        s.talkPose = kPoseStill;
        break;
    case 0x24:
    case 0x127:
    case 0x16b: s.talkPose = kPoseStill; break;
    default: break;
    }
    moveInFacing(player(), 0, 0, int16_t(-s.walkSpeed));
}
// 3 exe 0x5206d0: the stage animation 2, the faces and poses
void stepTronBeaten() {
    PokteScriptBytes& s = bytes();
    switch (stepTick()) {
    case 5:
        stageAnim(2);
        selectEyes(1);
        selectMouth(1);
        s.talkPose = kPoseFour;
        return;
    case 0x11: stopMouth(0x18); return;
    case 0x1d: selectMouth(1); return;
    case 0x30:
    case 0x8c:
        stopMouth(0x18);
        s.talkPose = kPoseStill;
        return;
    case 0x40:
        selectMouth(1);
        s.talkPose = kPoseFour;
        return;
    case 0x9b:
    case 0xc5: s.talkPose = kPoseNod; return;
    case 0xbb:
    case 0x155: s.talkPose = kPoseStill; return;
    default: return;
    }
}
// 8 exe 0x5208e0: the stage animation 3 (paused), the faces and poses
void stepTronLeaves() {
    PokteScriptBytes& s = bytes();
    switch (stepTick()) {
    case 0:
        stageAnim(3);
        stopEyes(0x1a);
        player().animPaused = 1;
        return;
    case 10: s.talkPose = kPoseNod; return;
    case 0x3e: s.talkPose = kPoseStill; return;
    case 0x4d:
        selectMouth(2);
        s.talkPose = kPoseFour;
        return;
    case 0x59: stopMouth(0x1b); return;
    case 0x65: selectMouth(2); return;
    case 0x79:
        stopMouth(0x1b);
        s.talkPose = kPoseStill;
        return;
    default: return;
    }
}
// 9 exe 0x520a30: the poses; Tron's cue at 0x6e; the next step at 0x118
void stepVillagersTalk() {
    PokteScriptBytes& s = bytes();
    switch (stepTick()) {
    case 10:
    case 0x80: s.talkPose = kPoseFive; return;
    case 0x30:
    case 0x76:
    case 0xaa:
    case 0xdd: s.talkPose = kPoseStill; return;
    case 0x3a:
    case 0xbe: s.talkPose = kPoseThree; return;
    case 0x6e: (*block().actors[kTronSlot]).params[0] = 1; return;
    case 0x118: scriptSequenceSkip(1); return;
    default: return;
    }
}
// 10 exe 0x520ba0: Tron placed, the player's stage animation 3; it turns (its spin 0x80, from 0x1e -0xc) between
// yaws 0x6c0 and 0x990
void stepTurnToVillage() {
    Player& p = player();
    const int t = stepTick();
    if (t == 0) {
        placeActor(scriptActor(kTronSlot), -0x2d4, -0x612, 0x44d, 0x410);
        stageAnim(3);
        p.animPaused = 0;
        p.acceleration.y = 0x80;
    } else if (t == 0x1e) {
        p.acceleration.y = -0xc;
    }
    const int16_t v = p.acceleration.y;
    const int16_t y = int16_t(uint16_t(uint16_t(p.rotation.y) + uint16_t(v)));
    p.rotation.y = y;
    if (v < 1) {
        if (y < 0x6c0) {
            p.rotation.y = 0x6c0;
            p.acceleration.y = 0;
        }
    } else if (y > 0x990) {
        p.rotation.y = 0x990;
        p.acceleration.y = 0;
    }
}
// 11 exe 0x520c50: the faces stopped
void stepFacesStop() {
    stopEyes(0x1b);
    stopMouth(0x15);
}

// the runner's dispatch (the two scripts share it)
void runStep(const PlayerEnv& env, const RoutineRef& routine) {
    switch (routine.id()) {
    case RoutineId::St25TronSceneGateOpensStep: stepGateOpens(); return;
    case RoutineId::St25TronSceneRunInStep: stepRunIn(env); return;
    case RoutineId::St25TronSceneSlowDownStep: stepSlowDown(env); return;
    case RoutineId::St25TronSceneAtGateStep: stepAtGate(); return;
    case RoutineId::St25TronSceneStep5: stepHiddenUntil(200, kGateX); return;
    case RoutineId::St25TronSceneFirstWordsStep: stepFirstWords(env); return;
    case RoutineId::St25TronSceneStep7: stepHiddenUntil(0x1cc, kGateX); return;
    case RoutineId::St25TronSceneSecondWordsStep: stepSecondWords(env); return;
    case RoutineId::St25TronSceneLongTalkStep: stepLongTalk(); return;
    case RoutineId::St25TronSceneStep10: stepHiddenUntil(0x195, 0); return;
    case RoutineId::St25TronSceneAnswerStep: stepAnswer(env); return;
    case RoutineId::St25TronSceneListenStep: stepListen(); return;
    case RoutineId::St25TronSceneTronTalksStep: stepTronTalks(); return;
    case RoutineId::St25TronSceneLastWordsStep: stepLastWords(); return;
    case RoutineId::St25TronSceneEndStep: stepEnd(); return;
    case RoutineId::St25TronSceneWreckStep: stepWreck(); return;
    case RoutineId::St25TronSceneTurnAndWalkStep: stepTurnAndWalk(); return;
    case RoutineId::St25TronSceneWalkToTronStep: stepWalkToTron(); return;
    case RoutineId::St25TronSceneTronBeatenStep: stepTronBeaten(); return;
    case RoutineId::EmptyRoutine: return; // the empty routine
    case RoutineId::St25TronSceneTronLeavesStep: stepTronLeaves(); return;
    case RoutineId::St25TronSceneVillagersTalkStep: stepVillagersTalk(); return;
    case RoutineId::St25TronSceneTurnToVillageStep: stepTurnToVillage(); return;
    case RoutineId::St25TronSceneFacesStopStep: stepFacesStop(); return;
    default: notPortedAt(kSt25Area, "script 0x1b / 0x1c step", routine.pointerValue());
    }
}
// exe 0x51f200 (state 1 of both scripts): the step's tick, id and sub for the cast; the routine (when it has one), the
// skip (on a frame of buffer 1: the skip's wipe), at the list's end or a skip state 2; the sequence, the face tracks
void steps(const PlayerEnv& env) {
    ScriptState& s = block();
    PokteScriptBytes& b = bytes();
    scriptState().part<CastTimelineStep>().stepId = s.stepId;
    scriptState().part<CastTimelineStep>().stepSub = s.stepSub;
    scriptState().part<CastTimelineTick>().stepTick = s.stepTick;
    const ScriptStep step = scriptStepAt(s.steps.value());
    bool toWrapUp = true;
    if (step.id != kScriptStepEnd) {
        if (const RoutineRef routine = stateRoutine(step.routine)) runStep(env, routine);
        toWrapUp = false;
        if (scriptSkipRequested() != 0 && display().drawBuffer != 0) {
            scriptFade(kScriptSkipWipe, 0);
            toWrapUp = true;
        }
    }
    if (toWrapUp) s.enterState(CutsceneState::WrapUp);
    scriptSequenceTick();
    faceTracksTick(s.faceTracks);
}

// ---- script 0x1b's states ----------------------------------------------------------------------------------------------
constexpr uint32_t kVoiceGate = 6;
constexpr uint32_t kVoiceFadeStep = 8;
constexpr uint16_t kSoundTankArrives = 0x2d;
// exe 0x51f0e0: the actors swept, the sequence on the replay pad, the player hidden at the gate's far side, its faces;
// once the player is script driven the voice; state 1 once it plays
void gateSetUp() {
    ScriptState& s = block();
    const uint8_t st = s.subState;
    if (st == 0) {
        sweepActors();
        scriptSequenceStart(stateAddress(TableId::St25TronScenesGateCamera), stateAddress(TableId::St25TronScenesGateSteps));
        scriptReplayPadOn();
        Player& p = player();
        p.clearDirectionOffsetWords();
        p.idleFaces[0] = 0;
        p.idleFaces[1] = 0;
        placePlayer(-0x100, -0x100, -0x1200, kHalfTurn);
        playerScriptAnim(PlayerAnim::Idle, 0, true);
        s.faceTracks.init(stateAddress(TableId::St25TronScenesGateEyesTable), stateAddress(TableId::St25TronScenesGateMouthTable));
        stopEyes(0x14);
        stopMouth(0x14);
        showPlayer(false);
        bytes().talkPose = kPoseStill;
        lockSkip(true);
        scriptSequenceTick();
        ++s.subState;
    } else if (st != 1) {
        if (st != 2 || !voiceClipPlaying()) return;
        s.enterState(CutsceneState::Steps);
        lockSkip(false);
        return;
    }
    if (playerToScriptIfFree() == 0) {
        voiceClipPlay(kVoiceGate);
        ++s.subState;
    }
}
// exe 0x51f270: the voice faded; once it is idle and no fade runs the tank arrives (a sound, the rumble, the warp to
// sub 6, a fade in); once taken the player back, the gate's cells open, the block's end
void gateWrapUp() {
    ScriptState& s = block();
    const uint8_t st = s.subState;
    if (st == 0) {
        if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFadeStep);
        ++s.subState;
    } else if (st != 1) {
        if (st != 2 || transitionRequest().state != kTransitionNone) return;
        playerBackToPlay();   // exe 0x5723c0
        scriptReplayPadOff(); // exe 0x511890
        Player& p = player();
        p.airborne = 0;
        p.animPaused = 0;
        showPlayer(true);
        switchGateCells(1);
        scriptEndBlock();
        return;
    }
    if (!voiceClipPlaying() && fadeState().idle()) {
        soundPlayFlatForced(kSoundTankArrives);
        padVibrate(0, 0x1000000);
        sweepActors();
        requestTransition(kTransitionWarp, kSt25, 6, 0, kGateY, kGateZ, kHalfTurn, kNoFade, kNoFade);
        scriptFade(2, 0);
        ++s.subState;
    }
}
void script1b(const PlayerEnv& env) {
    switch (block().cutsceneState()) {
    case CutsceneState::SetUp: gateSetUp(); return;
    case CutsceneState::Steps: steps(env); return;
    case CutsceneState::WrapUp: gateWrapUp(); return;
    default: notPortedAt(kSt25Area, "script 0x1b state", block().state);
    }
}

// ---- script 0x1c's states ----------------------------------------------------------------------------------------------
constexpr uint16_t kDatAfterBank = 0xc6, kDatVillage = 0xc9;
constexpr uint32_t kVoiceAfter = 7;
constexpr uint16_t kSoundVillageSaved = 0x2c;
constexpr size_t kTankLeftArmPart = 1, kTankRightArmPart = 5;
// exe 0x520070: the replay pad, the tank's broken arms noted, the music and the voice faded; the next bank's DAT once
// it is idle, the voice once it is in; once it plays and the player is script driven its faces, the sequence, state 1
void afterSetUp() {
    ScriptState& s = block();
    switch (s.subState) {
    case 0: {
        scriptReplayPadOn(); // exe 0x5117f0
        const AnimatedActor& tank = animatedActorAt(roomScriptState().actors[0]);
        PokteScriptBytes& b = bytes();
        b.brokenArms = 0;
        if (tank.parts[kTankLeftArmPart].flags & kPartHidden) b.brokenArms = 1;
        if (tank.parts[kTankRightArmPart].flags & kPartHidden) b.brokenArms |= 2u;
        b.talkPose = kPoseStill;
        lockSkip(true);
        soundFadeOut(kMusicFades, 0x222, 0);
        if (voiceClipPlaying()) voiceClipFadeOut(4);
        ++s.subState;
    }
        [[fallthrough]];
    case 1:
        if (!voiceClipPlaying()) {
            datLoadRequest(kDatAfterBank);
            ++s.subState;
        }
        return;
    case 2:
        if ((fileLoads().loading & kFileLoading) == 0) {
            voiceClipPlay(kVoiceAfter);
            ++s.subState;
        }
        return;
    case 3: {
        if (!voiceClipPlaying() || playerToScriptIfFree() != 0) return;
        s.enterState(CutsceneState::Steps);
        Player& p = player();
        p.idleFaces[0] = 0;
        p.idleFaces[1] = 0;
        p.clearDirectionOffsetWords();
        playerScriptAnim(PlayerAnim::Idle, 0, true);
        s.faceTracks.init(stateAddress(TableId::St25TronScenesAfterEyesTable), stateAddress(TableId::St25TronScenesAfterMouthTable));
        stopEyes(0x14);
        stopMouth(0x14);
        scriptSequenceStart(stateAddress(TableId::St25TronScenesAfterCamera), stateAddress(TableId::St25TronScenesAfterSteps));
        return;
    }
    default: return;
    }
}
// exe 0x5201b0: the rumble off, the voice faded, a fade out (a skip: the actors swept); once idle and the music faded
// the village's DAT, then the request to ST25 sub 0 at the gate (fade in 2); once taken the player back, the gate's
// cells closed, a sound, the next story, the block's end
void afterWrapUp() {
    ScriptState& s = block();
    switch (s.subState) {
    case 0:
        padVibrate(0, 0x1000000);
        if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFadeStep);
        if (!(s.flags & kScriptSkipTaken)) scriptFade(0x12, 0);
        else sweepActors();
        ++s.subState;
        [[fallthrough]];
    case 1:
        if (voiceClipPlaying() || !soundDone(kMusicFades) || !fadeState().idle()) return;
        datLoadRequest(kDatVillage);
        lockSkip(true);
        ++s.subState;
        [[fallthrough]];
    case 2:
        if (fileLoads().loading & kFileLoading) return;
        requestTransition(kTransitionAccepted, kSt25, 0, -0x100, kGateY, kGateZ, kHalfTurn, 2, kNoFade);
        ++s.subState;
        [[fallthrough]];
    case 3: {
        if (transitionRequest().state != kTransitionNone) return;
        lockSkip(false);
        playerBackToPlay();   // exe 0x5723c0
        scriptReplayPadOff(); // exe 0x511890
        Player& p = player();
        p.flags |= kActorLiveDrawn;
        p.airborne = 0;
        p.animPaused = 0;
        switchGateCells(0);
        soundPlayFlatForced(kSoundVillageSaved);
        ++gameState().nextStory; // exe 0x512800
        scriptEndBlock();
        return;
    }
    default: return;
    }
}
void script1c(const PlayerEnv& env) {
    switch (block().cutsceneState()) {
    case CutsceneState::SetUp: afterSetUp(); return;
    case CutsceneState::Steps: steps(env); return; // the shared runner exe 0x51f200
    case CutsceneState::WrapUp: afterWrapUp(); return;
    default: notPortedAt(kSt25Area, "script 0x1c state", block().state);
    }
}

// ---- the cast's routines by the script's step sub (exe data; the source names each routine) ------------------------
struct CueRoutines : DataRecord {
    static constexpr uint32_t kBytes = 0x400;
    std::array<uint32_t, 0x100> bySub{}; // +0x0
    template <class R> static CueRoutines load(const R& r) {
        CueRoutines v;
        for (size_t i = 0; i < v.bySub.size(); ++i) v.bySub[i] = r.template read<uint32_t>(0x0 + i * sizeof(uint32_t));
        return v;
    }
};
// the routine of the step sub (null: none; the source names it), nothing when the list is over
RoutineRef cueRoutine(uint32_t table) {
    return stateRoutine(dataRecord<CueRoutines>(table, CueRoutines::kBytes).bySub[scriptState().part<CastTimelineStep>().stepSub]);
}
bool listOver() { return scriptState().part<CastTimelineStep>().stepId == kScriptStepEnd; }
void selectTrack(KeyframeTracks& t, FaceTrack k, uint8_t seq) { t.track(k).select(seq, 0); }

// ---- Tron (exe 0x45af20; states [0x88f3fc]: bind 0x45afd0, run 0x45b060) -------------------------------------------
// the cast's block: its texture tracks (its face), castFlags & kCastHoldPose holds the pose, its start key
const DataTable<CollisionBoxRecord> tronBox{TableId::St25TronScenesBox};
void tronCue1(AnimatedActor& a, CastFields& f) {
    PokteScriptBytes& s = bytes();
    KeyframeTracks& t = f.faceTracks;
    auto stopCell = [&](FaceTrack k, uint32_t cell) { textureTrackStop(a, t, k, cell); }; // exe 0x5e3630 / 0x5e3660
    const int32_t k = scriptState().part<CastTimelineTick>().stepTick;
    if (k > 0x1b5) {
        if (k < 700) {
            if (k != 699) {
                switch (k) {
                case 0x1f0: break;
                case 0x1fa:
                    a.anim = 3;
                    selectTrack(t, FaceTrack::Eyes, 0);
                    selectTrack(t, FaceTrack::Mouth, 0);
                    s.talkPose = kPoseFive;
                    return;
                case 0x215: stopCell(FaceTrack::Mouth, 0); return;
                case 0x226: selectTrack(t, FaceTrack::Mouth, 0); return;
                case 0x276: stopCell(FaceTrack::Mouth, 0); break;
                case 0x280: s.talkPose = kPoseThree; return;
                default: return;
                }
            }
        } else {
            switch (k) {
            case 0x2c5:
                a.anim = 4;
                stopCell(FaceTrack::Eyes, 4);
                stopCell(FaceTrack::Mouth, 3);
                s.talkPose = kPoseFive;
                return;
            case 0x302:
                a.anim = 5;
                selectTrack(t, FaceTrack::Mouth, 1);
                return;
            case 0x316: selectTrack(t, FaceTrack::Mouth, 1); return;
            case 0x30c: stopCell(FaceTrack::Mouth, 4); return;
            case 0x34f: stopCell(FaceTrack::Mouth, 3); break;
            default: return;
            }
        }
        s.talkPose = kPoseStill;
        return;
    }
    if (k == 0x1b5) {
        s.talkPose = kPoseThree;
        return;
    }
    if (k > 0xf3) {
        switch (k) {
        case 0x11d:
        case 0x18b: stopCell(FaceTrack::Mouth, 0); return;
        case 0x131:
        case 0x191: selectTrack(t, FaceTrack::Mouth, 0); return;
        case 0x1ab:
            stopCell(FaceTrack::Mouth, 0);
            s.talkPose = kPoseStill;
            return;
        default: return;
        }
    }
    if (k == 0xf3) {
        a.anim = 2;
        selectTrack(t, FaceTrack::Mouth, 0);
        s.talkPose = kPoseFive;
        return;
    }
    switch (k) {
    case 0x14:
    case 0xbd: s.talkPose = kPoseThree; return;
    case 0x61:
    case 0xe9: s.talkPose = kPoseStill; return;
    case 0x6b:
        a.anim = 1;
        selectTrack(t, FaceTrack::Mouth, 0);
        s.talkPose = kPoseFive;
        return;
    case 0xb3:
        stopCell(FaceTrack::Mouth, 0);
        s.talkPose = kPoseStill;
        return;
    default: return;
    }
}
void tronCue2(AnimatedActor& a, CastFields& f) {
    PokteScriptBytes& s = bytes();
    KeyframeTracks& t = f.faceTracks;
    switch (scriptState().part<CastTimelineTick>().stepTick) {
    case 0:
        a.anim = 0xb;
        f.animKey = 0;
        f.castFlags |= kCastHoldPose;
        textureTrackStop(a, t, FaceTrack::Eyes, 5);
        textureTrackStop(a, t, FaceTrack::Mouth, 3);
        return;
    case 10:
        f.castFlags &= uint8_t(~kCastHoldPose);
        selectTrack(t, FaceTrack::Mouth, 1);
        s.talkPose = kPoseFive;
        return;
    case 0x23:
    case 0x8c: textureTrackStop(a, t, FaceTrack::Mouth, 4); return;
    case 0x28:
    case 0x55:
    case 0x91: selectTrack(t, FaceTrack::Mouth, 1); return;
    case 0x4b: textureTrackStop(a, t, FaceTrack::Mouth, 3); return;
    case 0xc0:
        textureTrackStop(a, t, FaceTrack::Mouth, 3);
        s.talkPose = kPoseStill;
        return;
    default: return;
    }
}
void tronCue3(AnimatedActor& a, CastFields& f) {
    PokteScriptBytes& s = bytes();
    KeyframeTracks& t = f.faceTracks;
    const int32_t k = scriptState().part<CastTimelineTick>().stepTick;
    if (k < 0xce) {
        switch (k) {
        case 0xcd: textureTrackStop(a, t, FaceTrack::Mouth, 5); return;
        case 0:
            textureTrackStop(a, t, FaceTrack::Eyes, 5);
            textureTrackStop(a, t, FaceTrack::Mouth, 3);
            return;
        case 10:
            a.anim = 6;
            f.animKey = 0;
            selectTrack(t, FaceTrack::Mouth, 1);
            s.talkPose = kPoseFive;
            return;
        case 0x61:
        case 0x96: textureTrackStop(a, t, FaceTrack::Mouth, 3); return;
        case 0x69:
        case 0x9b: selectTrack(t, FaceTrack::Mouth, 1); return;
        default: return;
        }
    }
    switch (k) {
    case 0xd7:
    case 0x16e: textureTrackStop(a, t, FaceTrack::Mouth, 3); return;
    case 0xf4: selectTrack(t, FaceTrack::Mouth, 1); return;
    case 0x16d:
        a.anim = 7;
        selectTrack(t, FaceTrack::Eyes, 1);
        return;
    case 0x17c: selectTrack(t, FaceTrack::Mouth, 2); return;
    case 0x1ba:
        textureTrackStop(a, t, FaceTrack::Mouth, 6);
        s.talkPose = kPoseStill;
        return;
    default: return;
    }
}
void tronCue4(AnimatedActor& a, CastFields& f) {
    PokteScriptBytes& s = bytes();
    KeyframeTracks& t = f.faceTracks;
    const int32_t k = scriptState().part<CastTimelineTick>().stepTick;
    if (k > 0x109) {
        switch (k) {
        case 0x112:
            a.parts[17].flags |= kPartHidden;
            a.parts[18].flags &= uint8_t(~kPartHidden);
            selectTrack(t, FaceTrack::Eyes, 2);
            textureTrackStop(a, t, FaceTrack::Mouth, 0xc);
            return;
        case 0x134:
        case 0x17a: selectTrack(t, FaceTrack::Mouth, 4); return;
        case 0x163: selectTrack(t, FaceTrack::Eyes, 3); return;
        case 0x168: textureTrackStop(a, t, FaceTrack::Mouth, 0xc); return;
        case 0x18d:
            textureTrackStop(a, t, FaceTrack::Mouth, 0xc);
            s.talkPose = kPoseStill;
            return;
        default: return;
        }
    }
    if (k == 0x109) {
        a.anim = 9;
        s.talkPose = kPoseFive;
        return;
    }
    switch (k) {
    case 0x55:
    case 0xa5: selectTrack(t, FaceTrack::Mouth, 3); return;
    case 0:
        a.anim = 7;
        f.animKey = 0x1b;
        textureTrackStop(a, t, FaceTrack::Mouth, 6);
        return;
    case 10:
        a.anim = 8;
        f.animKey = 0;
        a.parts[4].flags |= kPartHidden;
        a.parts[17].flags &= uint8_t(~kPartHidden);
        selectTrack(t, FaceTrack::Eyes, 4);
        selectTrack(t, FaceTrack::Mouth, 3);
        s.talkPose = kPoseFive;
        return;
    case 0x4b:
    case 0xa0: textureTrackStop(a, t, FaceTrack::Mouth, 9); return;
    case 0x100:
        textureTrackStop(a, t, FaceTrack::Mouth, 9);
        s.talkPose = kPoseStill;
        return;
    default: return;
    }
}
void tronCue5(AnimatedActor& a, CastFields& f) {
    PokteScriptBytes& s = bytes();
    KeyframeTracks& t = f.faceTracks;
    const int32_t k = scriptState().part<CastTimelineTick>().stepTick;
    if (k == 0) {
        textureTrackStop(a, t, FaceTrack::Eyes, 0xc);
        textureTrackStop(a, t, FaceTrack::Mouth, 0xc);
    } else if (k == 10) {
        a.anim = 10;
        f.animKey = 0;
        selectTrack(t, FaceTrack::Mouth, 1);
        s.talkPose = kPoseFive;
    } else if (k == 0x48) {
        textureTrackStop(a, t, FaceTrack::Mouth, 0xc);
        s.talkPose = kPoseStill;
    }
}
// exe 0x45b060: the step sub's routine [0x88f404 + sub * 4]
void tronCue(AnimatedActor& a, CastFields& f) {
    if (listOver()) return;
    const RoutineRef routine = cueRoutine(stateAddress(TableId::St25TronScenesTronCues));
    if (routine.null()) return;
    switch (routine.id()) {
    case RoutineId::St25TronTronCue1Cue: tronCue1(a, f); return;
    case RoutineId::St25TronTronCue2Cue: tronCue2(a, f); return;
    case RoutineId::St25TronTronCue3Cue: tronCue3(a, f); return;
    case RoutineId::St25TronTronCue4Cue: tronCue4(a, f); return;
    case RoutineId::St25TronTronCue5Cue: tronCue5(a, f); return;
    default: notPortedAt(kSt25Area, "0x45af20 step-sub function", routine.pointerValue());
    }
}
void tron(AnimatedActor& a) {
    CastFields& f = classState<CastFields>(a);
    a.savePreviousPose();
    a.prevAnim = a.anim;
    switch (a.state) {
    case 0: // exe 0x45afd0
        if (!bindActorModel(a, 0)) {
            freeActor(a); // exe 0x5e1490
            return;
        }
        a.clearClassBlock();
        a.collisionBox.pointAt(tronBox.stateAddress());
        a.anim = 0;
        a.prevAnim = kNoAnimShown;
        a.airborne = 0;
        f.faceTracks.init(stateAddress(TableId::St25TronScenesTronEyesTable), stateAddress(TableId::St25TronScenesTronMouthTable));
        textureTrackStop(a, f.faceTracks, FaceTrack::Eyes, 0);
        textureTrackStop(a, f.faceTracks, FaceTrack::Mouth, 0);
        a.enterState(1);
        tronCue(a, f);
        break;
    case 1: tronCue(a, f); break;
    default: notPortedAt(kSt25Area, "0x45af20 state", a.state);
    }
    if (!(a.flags & kActorLive)) return;
    textureTracksTick(a, f.faceTracks); // exe 0x5e34f0
    if (a.anim != a.prevAnim) {
        animSet(a, a.anim, int(f.animKey)); // exe 0x5e1c30
        animPose(a);
    } else if (f.castFlags & kCastHoldPose) {
        animDecodeCurrent(a); // exe 0x5e1f30
        animPose(a);
    } else {
        animStep(a); // exe 0x5e1e80
        animPose(a);
    }
}

const DataTable<CollisionBoxRecord> tankBox{TableId::St25TronScenesTankBox};
constexpr uint8_t kTankDustVariant = 7;
// exe 0x471690: from 0xf it falls ever faster to the ground (the shake, the rumble, 2 ticks of dust), then lands
void tankDrop(const PlayerEnv& env, AnimatedActor& a, TankLandingFields& f) {
    switch (a.subState) {
    case 0:
        f.dustTicks = 0;
        a.anim = 0x1e;
        a.velocity.y = 0;
        ++a.subState;
        break;
    case 1:
        if (scriptState().part<CastTimelineTick>().stepTick == 0xf) {
            a.velocity.y = 0x40;
            a.subState = 2;
        }
        break;
    case 2: {
        if (a.velocity.y < 0x200) a.velocity.y += 0x10u;
        a.pos.y.whole += uint16_t(a.velocity.y);
        if (actorTestMove(env, a) != 0) actorPushOut(env, a);
        const int16_t g = int16_t(actorGround(env, a));
        int d = int(a.pos.y.whole) - int(g);
        if (d < 0) d = int(g) - int(a.pos.y.whole);
        if (d < 0x40) {
            cameraShake(0, 0x2000, 0x100);
            padVibrate(0x801, 0x10220c0);
            a.pos.y.whole = g;
            ++a.subState;
            f.dustTicks = 2;
        }
        break;
    }
    case 3:
        if (a.animEnded()) {
            a.anim = 0x1f;
            a.subState = 4;
        }
        break;
    case 4:
        if (uint8_t(a.animKey.frame) == 8) f.flags |= kForceAnim;
        break;
    default: break;
    }
    if (f.dustTicks <= 0) return;
    for (int i = 0; i < 3; ++i) {
        const uint32_t slot = allocActorA0(0); // exe 0x5e13b0: the global skid sparks
        if (slot == 0) continue;
        EffectActor& e = effectActorAt(slot);
        e.type = typeByte(GlobalEffectType::SkidSparks);
        e.subtype = 0;
        e.flags |= kActorLive;
        e.variant = kTankDustVariant;
        const uint32_t r1 = rngStep();
        const uint16_t angle = uint16_t((uint32_t(i + 1) * 0x100 + (r1 & 0x3f) + uint16_t(a.rotation.y)) & 0xfff);
        const uint16_t r2 = uint16_t(rngStep());
        auto&& dust = classState<SkidSparkFields>(e);
        dust.yaw = angle;
        const uint32_t r3 = rngStep();
        const uint32_t k = (uint32_t(uint16_t(int16_t(angle) >> 6)) + (r3 & 7)) & 0x3f;
        const int32_t radius = int16_t((r2 & 0x1f) + 0x1f0);
        e.pos.x = int16_t(uint32_t(int16_t((int32_t(sin64(k)) * radius) >> 12)) + uint16_t(a.pos.x.whole));
        const uint32_t r4 = rngStep();
        e.pos.y = int16_t(-0x140 - int32_t(r4 & 0xf));
        e.pos.z = int16_t(uint32_t(uint16_t(a.pos.z.whole)) - uint16_t(int16_t((int32_t(cos64(k)) * radius) >> 12)));
    }
    f.dustTicks = f.dustTicks - 1;
}
// exe 0x4718f0: animation 0x1f, then 6 at its end
void tankStands(AnimatedActor& a, TankLandingFields& f) {
    if (scriptState().part<CastTimelineTick>().stepTick == 0) a.subState = 0;
    if (a.subState == 0) {
        f.flags &= uint8_t(~kForceAnim);
        a.anim = 0x1f;
        a.subState = 1;
    } else if (a.subState == 1 && a.animEnded()) {
        a.anim = 6;
        a.subState = 2;
    }
}
// exe 0x471950: animation 6 on the ground
void tankOnGround(const PlayerEnv& env, AnimatedActor& a) {
    if (scriptState().part<CastTimelineTick>().stepTick != 0) return;
    a.anim = 6;
    settle(env, a);
}
// exe 0x471660: the step sub's routine [0x89036c + sub * 4]
void tankCue(const PlayerEnv& env, AnimatedActor& a, TankLandingFields& f) {
    if (listOver()) return;
    const RoutineRef routine = cueRoutine(stateAddress(TableId::St25TronScenesTankCues));
    if (routine.null()) return;
    switch (routine.id()) {
    case RoutineId::St25TankTankDropCue: tankDrop(env, a, f); return;
    case RoutineId::St25TankTankStandsCue: tankStands(a, f); return;
    case RoutineId::St25TankTankOnGroundCue: tankOnGround(env, a); return;
    default: notPortedAt(kSt25Area, "0x471520 step-sub function", routine.pointerValue());
    }
}
void tankLanding(const PlayerEnv& env, AnimatedActor& a) {
    TankLandingFields& f = classState<TankLandingFields>(a);
    a.savePreviousPose();
    a.prevAnim = a.anim;
    switch (a.state) {
    case 0: // exe 0x4715d0
        if (!bindActorModel(a, 0)) {
            freeActor(a);
            return;
        }
        a.clearClassBlock();
        a.collisionBox.pointAt(tankBox.stateAddress());
        a.anim = 6;
        a.prevAnim = kNoAnimShown;
        a.velocity.clearWords();
        a.acceleration.clearWords();
        a.spin.clearWords();
        a.airborne = kAirborneFalling;
        f.startKey = 0;
        f.flags |= kTankShadow;
        a.enterState(1);
        tankCue(env, a, f);
        break;
    case 1: tankCue(env, a, f); break;
    default: notPortedAt(kSt25Area, "0x471520 state", a.state);
    }
    if (!(a.flags & kActorLive)) return;
    if (a.anim == a.prevAnim && !(f.flags & kForceAnim)) {
        animStep(a);
        animPose(a);
    } else {
        animSet(a, a.anim, int(f.startKey));
        animPose(a);
    }
    // kTankShadow: exe 0x5f20f0(actor, 0x100, 0x100, 0) the shadow (render output)
}

const DataTable<CollisionBoxRecord> wreckBox{TableId::St25TronScenesWreckBox};
constexpr uint8_t kPartMarkFirst = 1, kPartMarkLast = 0xd;
// the arms the script hands it (its +0xc bits): the parts they hide
constexpr size_t kLeftArmParts[4] = {1, 2, 3, 4}, kRightArmParts[4] = {5, 6, 7, 8};

BoneBlock& wreckBoneBlock(const AnimatedActor& a, uint32_t n) {
    BoneBlock* block = boneBlockOf(a, int(n));
    if (block == nullptr) notPorted(kSt25Area, "FUN_005e1780: no such bone block");
    return *block;
}
// exe 0x5e1300 + the common fields: a blast (the global static type 7)
StaticActor* wreckBlast() {
    const uint32_t slot = allocActor60(0);
    if (!slot) return nullptr;
    StaticActor& e = staticActorAt(slot);
    e.flags = kActorLive;
    e.type = typeByte(GlobalStaticType::Blast);
    e.loopFlags |= kActorSweepGroup2;
    return &e;
}
// exe 0x471f40: a marked part -- a blast at its bone at the first tick, 12 ticks of tint, a last blast and the part
// hidden
void wreckPart(AnimatedActor& a, WreckFields& f, uint32_t k) {
    BoneBlock& block = wreckBoneBlock(a, k >> 2);
    const ShortVec4& point = block.points[k & 3];
    const uint8_t c = f.partMarks[k];
    if (c == 0) return;
    if (c == 1) {
        if (StaticActor* e = wreckBlast()) {
            e->variant = 0;
            e->pos.x.whole = int16_t((rngStep() & 0x7f) + uint16_t(point.x) - 0x40u);
            e->pos.y.whole = int16_t((rngStep() & 0x7f) + uint16_t(point.y) - 0x40u);
            e->pos.z.whole = int16_t((rngStep() & 0x7f) + uint16_t(point.z) - 0x40u);
            const uint32_t r = rngStep();
            auto&& blast = BlastParams(actorHeaderAt(e->at));
            blast.attack = 0;
            blast.size = uint16_t((r & 0x1f) + 0x60);
        }
    } else if (c == kPartMarkLast) {
        if (StaticActor* e = wreckBlast()) {
            e->variant = 0;
            e->pos.x.whole = point.x;
            e->pos.y.whole = point.y;
            e->pos.z.whole = point.z;
            const uint32_t r = rngStep();
            auto&& blast = BlastParams(actorHeaderAt(e->at));
            blast.attack = 0;
            blast.size = uint16_t((r & 0x1f) + 0x90);
        }
        a.parts[k].flags |= kPartHidden;
        f.partMarks[k] = 0;
        return;
    }
    const uint8_t n = uint8_t(c + 1);
    f.partMarks[k] = n;
    block.colour(k & 3) = uint16_t(uint32_t(uint8_t(n + 0x12)) | 0x4200u);
}
// exe 0x471a50: the model (none: freed), its box and animation, a shadow, the base height, the bones' colours neutral,
// the arms already broken off hidden
void wreckInit(AnimatedActor& a) {
    if (!bindActorModel(a, 0)) {
        freeActor(a); // exe 0x5e1490
        return;
    }
    WreckFields& f = classState<WreckFields>(a);
    a.clearClassBlock();
    a.enterState(1);
    a.collisionBox.pointAt(wreckBox.stateAddress());
    a.anim = 0x24;
    a.prevAnim = kNoAnimShown;
    a.velocity.y = 0;
    f.startKey = 0;
    f.flags |= kTankShadow;
    f.baseY = int16_t(a.pos.y.whole + 0x40);
    a.tint = 0;
    for (uint32_t n = 0; n < 3; ++n)
        for (uint32_t j = 0; j < 4; ++j) wreckBoneBlock(a, n).colour(j) = kTintNeutral;
    wreckBoneBlock(a, 3).colour(0) = kTintNeutral;
    if (a.params[0] & 1)
        for (size_t p : kLeftArmParts) a.parts[p].flags |= kPartHidden;
    if (a.params[0] & 2)
        for (size_t p : kRightArmParts) a.parts[p].flags |= kPartHidden;
}
// a blast at the wreck's position, its fields after
StaticActor* wreckBlastAtBody(AnimatedActor& a, uint8_t variant) {
    StaticActor* e = wreckBlast();
    if (!e) return nullptr;
    e->variant = variant;
    e->pos = a.pos;
    return e;
}
// exe 0x471b80: keyed on the script's live step (sub 0: the parts' explosions by the tick, the broken arms' ticks give
// extra blasts, from 0x50 it sinks and tilts; sub 1: it sinks, a blast below it at 0x18, then four ticks of fading
// and freed); every marked part
void wreckRun(const PlayerEnv& env, AnimatedActor& a) {
    WreckFields& f = classState<WreckFields>(a);
    const ScriptState& s = block();
    const uint8_t phaseNow = a.subState;
    if (phaseNow == 1) {
        a.rotation.x = int16_t(a.rotation.x - 0x20);
        a.velocity.y = -0x100;
        integrateVelocity(a, 0, -0x100, 0);
        if (s.stepTick == 0x18) {
            if (StaticActor* e = wreckBlastAtBody(a, 1)) {
                e->pos.y.whole = int16_t(uint32_t(actorGround(env, a)) - 0x40u); // exe 0x40f190
                auto&& blast = BlastParams(actorHeaderAt(e->at));
                blast.size = 0x170;
                blast.attack = 0;
            }
            a.subState = 2;
            a.step = 0;
            a.stepCounter = 0;
        }
    } else if (phaseNow == 2) {
        if (a.stepCounter == 0) {
            padVibrate(0x1001, 0x10430e0);
            ++a.stepCounter;
        }
        a.rotation.x = int16_t(a.rotation.x - 0x20);
        ++a.step;
        wreckBoneBlock(a, 0).colour(0) = uint16_t((uint32_t(a.step) * 4 + 0x10) | 0x4200u);
        if (a.step == 4) {
            freeActor(a); // exe 0x5e1490
            return;
        }
    } else if (phaseNow == 0) {
        if (s.stepSub == 0) {
            const uint32_t t = uint32_t(s.stepTick);
            bool blast = (t & (int32_t(t) > 0x17 ? 0x1fu : 3u)) == 1;
            const uint8_t arms = a.params[0];
            switch (t) {
            case 0x1a: if (arms & 1) blast = true; else f.partMarks[4] = 1; break;
            case 0x22: if (!(arms & 1)) f.partMarks[3] = 1; break;
            case 0x24: if (arms & 2) blast = true; else f.partMarks[8] = 1; break;
            case 0x2a: if (arms & 1) blast = true; else f.partMarks[2] = 1; break;
            case 0x2c: if (!(arms & 2)) f.partMarks[7] = 1; break;
            case 0x32: if (!(arms & 1)) f.partMarks[1] = 1; break;
            case 0x34: if (arms & 2) blast = true; else f.partMarks[6] = 1; break;
            case 0x3c: if (!(arms & 2)) f.partMarks[5] = 1; break;
            case 0x42: f.partMarks[0xb] = 1; break;
            case 0x46: f.partMarks[9] = 1; break;
            case 0x4a: f.partMarks[0xa] = 1; break;
            case 0x4e: f.partMarks[0xc] = 1; break;
            default: break;
            }
            if (s.stepTick > 0x50) {
                a.velocity.y = int16_t(a.velocity.y + 8);
                a.rotation.x = int16_t(a.rotation.x - 0x20);
            }
            if (blast) {
                if (StaticActor* e = wreckBlastAtBody(a, 0)) {
                    e->pos.x.whole = int16_t(uint16_t(e->pos.x.whole) + ((rngStep() & 0x1ff) - 0x100u));
                    e->pos.y.whole = int16_t(uint16_t(e->pos.y.whole) + ((rngStep() & 0x1ff) - 0x240u));
                    e->pos.z.whole = int16_t(uint16_t(e->pos.z.whole) + ((rngStep() & 0x1ff) - 0x100u));
                    const uint32_t r = rngStep();
                    auto&& params = BlastParams(actorHeaderAt(e->at));
                    params.attack = 0;
                    params.size = uint16_t((r & 0x1f) + 0x40);
                }
            }
        } else if (s.stepSub == 1) {
            a.velocity.y = int16_t(a.velocity.y + 8);
            a.rotation.x = int16_t(a.rotation.x - 0x20);
            a.subState = 1;
            a.step = 0;
        }
        integrateVelocity(a, 0, a.velocity.y, 0);
    }
    for (uint32_t k = kPartMarkFirst; k < kPartMarkLast; ++k)
        if (f.partMarks[k] != 0) wreckPart(a, f, k);
}
void tankWreck(const PlayerEnv& env, AnimatedActor& a) {
    a.savePreviousPose();
    a.prevAnim = a.anim;
    if (a.state == 0) wreckInit(a);
    else wreckRun(env, a);
    if (!(a.flags & kActorLive)) return;
    WreckFields& f = classState<WreckFields>(a);
    if (a.anim == a.prevAnim && !(f.flags & kForceAnim)) {
        animStep(a); // exe 0x5e1e80
        animPose(a);
    } else {
        animSet(a, a.anim, int(f.startKey)); // exe 0x5e1c30
        animPose(a);
    }
    // the shadow flag: exe 0x5f20f0(actor, 0x100, 0x100, 0) the shadow (render output)
}

const DataTable<CollisionBoxRecord> figureBox{TableId::St25TronScenesFigureBox};
constexpr uint8_t kFigureSparkVariant = 5;
constexpr uint32_t kFigureSparkBones = 0xf;

// exe 0x4668d0(point, yaw, variant): a spark of the global skid sparks at the point, thrown along the yaw
void spark(const int16_t point[3], uint16_t yaw, uint8_t variant) {
    const uint32_t slot = allocActorA0(0); // exe 0x5e13b0
    if (!slot) return;
    EffectActor& e = effectActorAt(slot);
    e.type = typeByte(GlobalEffectType::SkidSparks);
    e.flags |= kActorLive;
    e.variant = variant;
    e.subtype = 0;
    auto&& sparkFields = classState<SkidSparkFields>(e);
    sparkFields.yaw = yaw;
    e.pos.x = point[0];
    e.pos.y = point[1];
    e.pos.z = point[2];
}
// the figure's spark: at the bone (counter >> 1) % 15, thrown to its right
void figureSpark(AnimatedActor& a, FigureFields& f) {
    const int32_t n = int32_t(f.sparkCounter) >> 1;
    const ShortVec4& point = bonePoint(a, uint32_t(n % int32_t(kFigureSparkBones)), kSt25Area); // exe 0x5e1810
    const int16_t p[3] = {point.x, point.y, point.z};
    spark(p, uint16_t((a.rotation.y - kQuarterTurn) & kAngleMask), kFigureSparkVariant);
}
// action 1 (exe 0x45be60): hidden below the ground, it springs out at 0x1f and hops off in arcs along yaw 0x300,
// rolling, sparking every fourth tick
void figureHopsOff(const PlayerEnv& env, AnimatedActor& a, FigureFields& f) {
    const int g = actorGround(env, a); // exe 0x40f190
    // the arc of phases 2..5: the vertical speed, the roll, the move along yaw 0x300, the landing at the hover height
    auto arc = [&](int16_t nextY, int16_t nextZ) {
        a.velocity.y += 0x30u;
        a.rotation.z = int16_t(uint32_t(a.rotation.z - 0x180) & kAngleMask);
        moveAlongYaw(a.pos, 0x300, 0, a.velocity.y, a.velocity.z); // exe 0x5e36f0
        if (g - f.hover <= a.pos.y.whole) {
            ++a.subState;
            a.velocity.z = nextZ;
            a.velocity.y = nextY;
            a.pos.y.whole = int16_t(g - f.hover);
        }
    };
    auto settleRoll = [&]() {
        if (f.hover >= 0x11) f.hover = int16_t(f.hover - 4);
        if (a.rotation.x <= 0x3ff) a.rotation.x += 0x20u;
    };
    switch (a.subState) {
    case 0:
        a.anim = 1;
        a.flags &= uint8_t(~kActorDrawn);
        a.pos.y.whole = int16_t(actorGround(env, a) - 0x40);
        ++a.subState;
        [[fallthrough]];
    case 1:
        f.sparkCounter = 1;
        if (scriptState().part<CastTimelineTick>().stepTick == 0x1f) {
            a.flags |= kActorDrawn;
            a.airborne = kAirborneFalling;
            a.rotation.y = int16_t(uint32_t(a.rotation.y - 0x100) & kAngleMask);
            a.rotation.z = kHalfTurn;
            f.hover = 0x40;
            f.flags |= kFigureShadow;
            f.shadowSize = 0x30;
            a.velocity.z = -0x200;
            a.velocity.y = -0x3c0;
            a.pos.y.whole = int16_t(g - 0x60);
            ++a.subState;
        }
        break;
    case 2:
        f.hover = 0x40;
        arc(-0x1f0, -0x110);
        break;
    case 3:
        settleRoll();
        arc(-0x110, -0xa0);
        break;
    case 4:
        settleRoll();
        arc(-0xa8, -0x70);
        break;
    case 5:
        f.hover = 0x10;
        a.velocity.y += 0x30u;
        a.rotation.z = int16_t(uint32_t(a.rotation.z - 0x180) & kAngleMask);
        a.rotation.x = kQuarterTurn;
        moveAlongYaw(a.pos, 0x300, 0, a.velocity.y, a.velocity.z);
        if (g - f.hover <= a.pos.y.whole) {
            ++a.subState;
            a.velocity.z = 0;
            a.velocity.y = 0;
            a.pos.y.whole = int16_t(g - f.hover);
            a.airborne = 0;
        }
        break;
    case 6: {
        const int r = a.rotation.z;
        if (r >= 0x1af && r <= 0x72f) {
            a.rotation.z = 0x5b0;
            ++a.subState;
        } else {
            a.rotation.z = int16_t(uint32_t(r - 0x180) & kAngleMask);
        }
        break;
    }
    default: break;
    }
    if (!(uint16_t(f.sparkCounter) & 3)) figureSpark(a, f);
    ++f.sparkCounter;
}
// action 2 (exe 0x45c1e0): at tick 0 animation 2, level, facing 0x950
void figureFacesVillage(AnimatedActor& a) {
    if (scriptState().part<CastTimelineTick>().stepTick != 0) return;
    a.anim = 2;
    a.rotation.x = 0;
    a.rotation.y = 0x950;
    a.rotation.z = 0;
}
// action 3 (exe 0x45c210): sparks every other tick
void figureSparks(AnimatedActor& a, FigureFields& f) {
    if (scriptState().part<CastTimelineTick>().stepTick == 0) f.sparkCounter = 0;
    if (uint16_t(f.sparkCounter) & 1) figureSpark(a, f);
    ++f.sparkCounter;
}
// action 4 (exe 0x45c2a0): placed at tick 0, the talking poses, sparks every other tick
void figureTalksSparking(AnimatedActor& a, FigureFields& f) {
    PokteScriptBytes& s = bytes();
    const int32_t t = scriptState().part<CastTimelineTick>().stepTick;
    if (t == 0x92 || t == 10) s.talkPose = kPoseThree;
    else if (t == 0x102 || t == 0x88) s.talkPose = kPoseStill;
    else if (t == 0) {
        placeActor(a, -0x5a6, -0xf8, -0x2a6, 0x8e0);
        f.sparkCounter = 0;
    }
    if (uint16_t(f.sparkCounter) & 1) figureSpark(a, f);
    ++f.sparkCounter;
}
// the tail of actions 5 / 6: forward by its speed, on the ground
void figureDrives(const PlayerEnv& env, AnimatedActor& a) {
    integrateVelocity(a, 0, 0, -int(a.velocity.z));
    a.pos.y.whole = int16_t(actorGround(env, a)); // exe 0x40f190
}
// action 5 (exe 0x45c380, jump table 0x45c41c on the tick read unsigned): placed, stops at 0x78, drives at 0x8d
void figureDrivesOff(const PlayerEnv& env, AnimatedActor& a, FigureFields& f) {
    switch (uint32_t(scriptState().part<CastTimelineTick>().stepTick)) {
    case 0: placeActor(a, -0x666, -0x126, -0x2a6, 0x950); break;
    case 0x78:
        a.anim = 3;
        a.velocity.z = 0;
        f.shadowSize = 0x20;
        break;
    case 0x8d: a.velocity.z = 0x116; break;
    case 0x97: a.anim = 4; break;
    default: break;
    }
    figureDrives(env, a);
}
// action 6 (exe 0x45c4c0): drives, slows at 0x2f, stops at 0x37
void figureDrivesOn(const PlayerEnv& env, AnimatedActor& a) {
    const int32_t t = scriptState().part<CastTimelineTick>().stepTick;
    if (t == 0) {
        a.velocity.z = 0x116;
    } else if (t == 0x2f) {
        a.anim = 5;
        a.velocity.z = 0x106;
    } else if (t == 0x37) {
        a.velocity.z = 0;
    }
    figureDrives(env, a);
}
// action 7 (exe 0x45c520): animation 6, the talking poses
void figureTalks(AnimatedActor& a) {
    PokteScriptBytes& s = bytes();
    switch (scriptState().part<CastTimelineTick>().stepTick) {
    case 0xfb:
    case 0x189:
    case 0x5b: s.talkPose = kPoseStill; break;
    case 0x12f:
    case 0x87:
    case 10: s.talkPose = kPoseFive; break;
    case 0: a.anim = 6; break;
    default: break;
    }
}
// exe 0x45be30: unless the list is over, the action of the step sub ([0x88f490 + sub * 4]; entries 0 / 8 / 9 null)
void figureAction(const PlayerEnv& env, AnimatedActor& a, FigureFields& f) {
    if (listOver()) return;
    switch (scriptState().part<CastTimelineStep>().stepSub) {
    case 0:
    case 8:
    case 9: return;
    case 1: figureHopsOff(env, a, f); return;
    case 2: figureFacesVillage(a); return;
    case 3: figureSparks(a, f); return;
    case 4: figureTalksSparking(a, f); return;
    case 5: figureDrivesOff(env, a, f); return;
    case 6: figureDrivesOn(env, a); return;
    case 7: figureTalks(a); return;
    default: notPortedAt(kStoryPort, "0x45bce0 action (table 0x88f490)", scriptState().part<CastTimelineStep>().stepSub);
    }
}
void script1cFigure(const PlayerEnv& env, AnimatedActor& a) {
    FigureFields& f = classState<FigureFields>(a);
    a.savePreviousPose();
    a.prevAnim = a.anim;
    switch (a.state) {
    case 0: // exe 0x45bda0
        if (!bindActorModel(a, a.variant)) { // exe 0x5e0bc0
            freeActor(a);
            break;
        }
        a.clearClassBlock();
        a.collisionBox.pointAt(figureBox.stateAddress());
        a.anim = 0;
        a.prevAnim = kNoAnimShown;
        a.airborne = 0;
        f.tracks.init(stateAddress(TableId::St25TronScenesFigureEyesTable), stateAddress(TableId::St25TronScenesFigureMouthTable)); // exe 0x5e33a0
        textureTrackStop(a, f.tracks, FaceTrack::Eyes, 0xf);  // exe 0x5e3630
        textureTrackStop(a, f.tracks, FaceTrack::Mouth, 0xf); // exe 0x5e3660
        a.enterState(1);
        figureAction(env, a, f);
        break;
    case 1: figureAction(env, a, f); break;
    default: notPortedAt(kStoryPort, "0x45bce0 state (table 0x88f488)", a.state);
    }
    if (!(a.flags & kActorLive)) return;
    textureTracksTick(a, f.tracks); // exe 0x5e34f0
    if (a.anim != a.prevAnim) animSetPose(a, a.anim, f.startKey); // exe 0x5e1c30
    else if (f.flags & kFigureHoldPose) animDecodeCurrent(a);      // exe 0x5e1f30
    else animStepPose(a);                                           // exe 0x5e1e80
    // kFigureShadow: exe 0x5f20f0(actor, shadowSize, 0x100, 0), the shadow (render output)
}

} // namespace

bool st25TronScriptPorted(RoutineId handler) { return handler == RoutineId::St25TronScript1b || handler == RoutineId::St25TronScript1c; }
bool st25TronScriptTick(const PlayerEnv& env, RoutineId handler) {
    if (handler == RoutineId::St25TronScript1b) script1b(env);
    else if (handler == RoutineId::St25TronScript1c) script1c(env);
    else return false;
    return true;
}

void st25TronUpdate(const PlayerEnv&, uint32_t actor) { tron(animatedActorAt(actor)); }
void st25TankLandingUpdate(const PlayerEnv& env, uint32_t actor) { tankLanding(env, animatedActorAt(actor)); }
void st25TankWreckUpdate(const PlayerEnv& env, uint32_t actor) { tankWreck(env, animatedActorAt(actor)); }
void st25Script1cFigureUpdate(const PlayerEnv& env, uint32_t actor) { script1cFigure(env, animatedActorAt(actor)); }

} // namespace od::port
