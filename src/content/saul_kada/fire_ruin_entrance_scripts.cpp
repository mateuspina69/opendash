// The fire ruin's story-13 cutscene scripts (hypothesis names): script 0x40 (exe 0x52ce90, ST27 sub 8, the first
// visit of the entrance), 0x41 (exe 0x52d5a0, ST27 sub 8 at flag 0x4c, the escape's end: the gate opens), 0x42 (exe
// 0x52de50, ST28 sub 5, the first visit of the depths), 0x43 (exe 0x52e2e0, ST28 sub 5 at flag 0x4e, the falling
// rubble). Each handler dispatches on the script's state: the set-up, the steps runner (the steps dispatched by their
// named routines), the wrap-up.
#include "content/saul_kada/fire_ruin_entrance_scripts.hpp"

#include "engine/data_source.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/camera.hpp"
#include "game/camera_state.hpp"
#include "game/display.hpp"
#include "game/explosions.hpp"
#include "game/fade_state.hpp"
#include "game/global_class_types.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/message.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/spark_emitter.hpp"
#include "game/spawn.hpp"
#include "game/stage_files.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/table_records.hpp"
#include "game/transition.hpp"
#include "game/dat_names.hpp"

namespace od::port {

namespace {

constexpr uint8_t kSkipFade = 0x11, kSkippedFade = 1;
constexpr uint32_t kVoiceFade = 8;
constexpr RoutineId kStepIdle = RoutineId::EmptyRoutine;
constexpr uint16_t kStepSound = 0x91;
constexpr uint16_t kRuinMusic = 0x3c;
// the opening's steps (the script's sub-state) of scripts 0x40 / 0x41
enum class Opening : uint8_t { Start = 0, TakeOver = 1, WaitFiles = 2, Voice = 3, WaitVoice = 4 };
enum class WrapUp : uint8_t { FadeVoice = 0, WaitVoice = 1, End = 2 };
// the walk-in's phases (the script's sub-state); Done and after belong to the step that runs it
enum class WalkIn : uint8_t { Start = 0, Walk = 1, Turn = 2, Slow = 3, Done = 4 };

// ---- the shared parts ------------------------------------------------------------------------------------------
int32_t tick() { return scriptState().stepTick; }
bool fileLoading() { return (fileLoads().loading & kFileLoading) != 0; }
bool streamBusy() { return (fileLoads().loading & kStreamBusy) != 0; }
void nextPhase() { scriptState().subState += 1u; }
void nextStepPhase() { scriptState().stepPhase += 1u; }
void playerAnim(uint8_t anim) { playerScriptAnimId(anim, 0, 1); } // exe 0x5723d0(anim, 0, 1)
uint8_t playerKeyFrame() { return uint8_t(player().body.key.frame); }
bool playerKeyHeld() { return (player().body.key.flags & 0x80) != 0; }
void walkBack(int speed) { moveAlongYaw(player().pos, player().rotation.y, 0, 0, speed); } // exe 0x5e36f0
void setPlayerPos(uint32_t x, uint32_t y, uint32_t z, int16_t yaw) {
    Player& p = player();
    p.pos.x = int32_t(x);
    p.pos.y = int32_t(y);
    p.pos.z = int32_t(z);
    p.rotation.y = yaw;
}
// the player placed for a cutscene (the exe's inline block): his move angles and idle faces cleared, his position, yaw
void placePlayer(uint32_t x, uint32_t y, uint32_t z, int16_t yaw) {
    Player& p = player();
    p.clearDirectionOffsetWords();
    p.idleFaces[0] = 0;
    p.idleFaces[1] = 0;
    setPlayerPos(x, y, z, yaw);
}
// the player back at the end (on the ground) at his place
void playerBackAt(uint32_t x, uint32_t y, uint32_t z, int16_t yaw) {
    playerBackToPlay();   // exe 0x5723c0
    scriptReplayPadOff(); // exe 0x511890
    player().airborne = 0;
    setPlayerPos(x, y, z, yaw);
}
// the face tracks (exe 0x5e33a0(first, second)), both on sequence 0 key 0
void facesInit(uint32_t first, uint32_t second) {
    KeyframeTracks& faces = scriptState().faceTracks;
    faces.init(first, second);
    faces.first.select(0, 0);  // exe 0x5e33d0
    faces.second.select(0, 0); // exe 0x5e33f0
}
// the walk-ins' footstep: the player's animation `anim` at frames 2 / 0xd / 0x16
void footstep(uint8_t anim) {
    if (player().body.anim != anim) return;
    const uint8_t frame = playerKeyFrame();
    if (frame == 2 || frame == 0xd || frame == 0x16) soundPlayAtPlayer(kStepSound); // exe 0x5d53e0
}
// the walk-in of steps exe 0x52d160 / 0x52e030 / 0x52e760: animation 1, walking -0x200 a tick until tick `turnAt`
// (animation `turn`), until its frame 3, then -0x76 until frame 0x11; true once done (the caller's tail)
bool walkIn(int32_t turnAt, uint8_t turn) {
    switch (WalkIn(scriptState().subState)) {
    case WalkIn::Start:
        playerAnim(1);
        nextPhase();
        return false;
    case WalkIn::Walk:
        walkBack(-0x200);
        if (tick() == turnAt) {
            playerAnim(turn);
            nextPhase();
        }
        return false;
    case WalkIn::Turn:
        walkBack(-0x200);
        if (playerKeyFrame() == 3) nextPhase();
        return false;
    case WalkIn::Slow:
        walkBack(-0x76);
        if (playerKeyFrame() == 0x11) nextPhase();
        return false;
    default: return true;
    }
}
bool walkInDone() { return WalkIn(scriptState().subState) == WalkIn::Done; }
// the tail of a step routine: at the tick one step ahead (exe 0x5115c0(1)), the sub-state cleared
void skipAt(int32_t at) {
    if (tick() != at) return;
    scriptSequenceSkip(1);
    scriptState().subState = 0;
}
// exe 0x52d020 (scripts 0x40 / 0x41 / 0x42): the step's routine, the skip -> fade 0x11 and the wrap-up; the list's end
// -> the wrap-up; the sequence and the face tracks tick
void runSteps(ScriptStepRoutines routines) {
    ScriptState& s = scriptState();
    const ScriptStep step = scriptStepAt(s.steps.value());
    if (step.id == kScriptStepEnd) {
        s.enterState(CutsceneState::WrapUp);
    } else {
        routines(stateRoutine(step.routine));
        if (scriptSkipRequested() && display().drawBuffer != 0) { // exe 0x511790
            scriptFade(kSkipFade, 0);                              // exe 0x53f6b0
            s.enterState(CutsceneState::WrapUp);
        }
    }
    scriptSequenceTick();           // exe 0x5114d0
    faceTracksTick(s.faceTracks);   // exe 0x5e3410
}
// the head the wrap-ups of scripts 0x40 / 0x41 / 0x42 share (exe 0x52d080 / 0x52d750 / 0x52df50): the voice faded out,
// then idle with no fade running; true once there
bool wrapUpReady() {
    ScriptState& s = scriptState();
    switch (WrapUp(s.subState)) {
    case WrapUp::FadeVoice:
        if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade); // exe 0x601220
        nextPhase();
        return false;
    case WrapUp::WaitVoice:
        if (voiceClipIdle() && fadeState().busy == 0) nextPhase();
        return false;
    case WrapUp::End: return true;
    default: return false;
    }
}
// the wrap-ups' start: the skip's fade 1, the actors swept
void wrapUpStart() {
    if (scriptState().flags & kScriptSkipTaken) scriptFade(kSkippedFade, 0);
    actorsSweep(kSweepActors, kSweepFree);
}
// the start of the set-ups: the replay pad, the music faded out (and the windows closed)
void setUpStart(bool closeWindows) {
    scriptReplayPadOn();            // exe 0x5117f0
    soundFadeOut(kMusicFades, 0xfff, 0); // exe 0x5d5780
    if (closeWindows) windowCloseAll(WindowClose::Force); // exe 0x5577d0
}
// the opening of scripts 0x40 / 0x41 (exe 0x52ceb0 / 0x52d5c0): the start, the take-over (once the player is the
// script's), the music done and no file loading, the voice once the stream is free; playing -> the steps
template <class TakeOver> void opening(TakeOver takeOver, uint32_t voice) {
    ScriptState& s = scriptState();
    switch (Opening(s.subState)) {
    case Opening::Start:
        setUpStart(true);
        s.subState = uint8_t(Opening::TakeOver);
        [[fallthrough]];
    case Opening::TakeOver:
        if (playerToScriptIfFree() != 0) return; // exe 0x572300
        actorsSweep(kSweepActors, kSweepFree);   // exe 0x5e05d0
        takeOver();
        s.subState = uint8_t(Opening::WaitFiles);
        return;
    case Opening::WaitFiles:
        if (soundDone(kMusicFades) && !fileLoading()) s.subState = uint8_t(Opening::Voice); // exe 0x5d5900
        return;
    case Opening::Voice:
        if (streamBusy()) return;
        voiceClipPlay(voice); // exe 0x601050
        s.subState = uint8_t(Opening::WaitVoice);
        return;
    case Opening::WaitVoice:
        if (voiceClipPlaying()) s.enterStateKeepingPhases(uint8_t(CutsceneState::Steps));
        return;
    default: return;
    }
}
// a spark emitter (the stage's static type 3) of the cutscenes at the s16 point: no drift, its lifetime
uint32_t ember(uint8_t variant, int16_t x, int16_t y, int16_t z, bool sweepGroup) {
    const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!slot) return 0;
    StaticActor& spark = staticActorAt(slot);
    spark.flags = kActorLive;
    if (sweepGroup) spark.loopFlags |= kActorSweepGroup2;
    spark.type = kSparkEmitterStageType;
    spark.variant = variant;
    spark.pos.x.whole = x;
    spark.pos.y.whole = y;
    spark.pos.z.whole = z;
    return slot;
}
void emberParams(uint32_t slot, int16_t lifetime) {
    SparkEmitterParams& params = classState<SparkEmitterParams>(staticActorAt(slot));
    params.driftX = 0;
    params.driftZ = 0;
    params.lifetime = lifetime;
}

// ---- script 0x40 (exe 0x52ce90, states [..], camera 0x8a8214, steps 0x8a83e8) ----------------------------------------
// ST27 sub 8, the first visit: Megaman walks in backwards from (-0x400, 0, 0xad9) (animation 0x81, then 0x50 once its
// key holds), two embers at tick 0x14 of the third step, the face tracks 0x8a845c / 0x8a84b0 and the voice 0x16. The
// end: records 0x8a8148 x 6 / 0x8a81d8 x 3, the player at (-0x400, 0, 0xa00) yaw 0, the ruin's music.
constexpr RoutineId kEntranceWalkIn = RoutineId::FireRuinEntranceScriptsEntranceWalkIn, kEntranceFaces = RoutineId::FireRuinEntranceScriptsEntranceFaces, kEntranceEmbers = RoutineId::FireRuinEntranceScriptsEntranceEmbers, kEntranceTalk = RoutineId::FireRuinEntranceScriptsEntranceTalk, kEntranceStepUp = RoutineId::FireRuinEntranceScriptsEntranceStepUp, kEntranceMouth = RoutineId::FireRuinEntranceScriptsEntranceMouth;
constexpr uint8_t kAnimEntranceWalk = 0x81, kAnimStand = 0x50, kAnimStepUp = 0x82;
constexpr uint8_t kEntranceEmber = 0x17;
constexpr int16_t kEntranceEmberLife = 0x3c;

// exe 0x52d560(point): an ember (variant 0x17) at the point, lifetime 0x3c
void entranceEmber(int16_t x, int16_t y, int16_t z) {
    if (const uint32_t slot = ember(kEntranceEmber, x, y, z, false)) emberParams(slot, kEntranceEmberLife);
}
void entranceStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    KeyframeTracks& faces = s.faceTracks;
    const int32_t t = tick();
    switch (routine.id()) {
    case kEntranceWalkIn:
        footstep(kAnimEntranceWalk);
        if (!walkIn(0x31, kAnimEntranceWalk) || !walkInDone()) return;
        if (s.stepPhase == 0 && playerKeyHeld()) {
            playerAnim(kAnimStand);
            nextStepPhase();
        }
        if (t == 0x78) {
            scriptSequenceSkip(1); // exe 0x5115c0
            s.subState = 0;
            s.stepPhase = 0;
        }
        return;
    case kEntranceFaces:
        if (t == 0x6e) faces.second.select(1, 0);
        else if (t == 0xbe) faces.second.select(0, 0);
        else if (t == 0xc8) scriptSequenceSkip(1);
        return;
    case kEntranceEmbers:
        if (t == 0x14) {
            entranceEmber(int16_t(0xfae0), int16_t(0xfe80), 0x20);
            entranceEmber(int16_t(0xfb20), int16_t(0xfe80), 0x20);
        }
        return;
    case kEntranceTalk:
        if (t == 0x78) {
            faces.second.select(1, 0);
        } else if (t == 0xb4) {
            faces.first.select(1, 0);
        } else if (t == 0x101) {
            faces.second.select(0, 0);
        } else if (t == 0x10e) {
            faces.first.select(2, 0);
            faces.second.select(2, 0);
            scriptSequenceSkip(1);
        }
        return;
    case kEntranceStepUp:
        switch (s.subState) {
        case 0:
            if (t == 0x6e) {
                playerAnim(kAnimStepUp);
                nextPhase();
            }
            return;
        case 1:
            walkBack(0x15);
            if (playerKeyFrame() == 0x18) nextPhase();
            return;
        case 2:
            if (playerKeyHeld()) {
                playerAnim(kAnimStand);
                nextPhase();
            }
            return;
        default: return;
        }
    case kEntranceMouth:
        if (t == 0x10e) faces.second.select(3, 0);
        else if (t == 0x133) faces.second.select(2, 0);
        return;
    case kStepIdle: return;
    default: notPortedAt(kStoryPort, "script 0x40 step", routine.pointerValue());
    }
}
void entranceScript() { // exe 0x52ce90
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x52ceb0
        opening(
            [] {
                placePlayer(0xfc000000, 0, 0xad90000, 0);
                facesInit(stateAddress(TableId::FireRuinEntranceScriptsEntranceFirstTracks), stateAddress(TableId::FireRuinEntranceScriptsEntranceSecondTracks));
                scriptSequenceStart(stateAddress(TableId::FireRuinEntranceScriptsEntranceCamera), stateAddress(TableId::FireRuinEntranceScriptsEntranceSteps)); // exe 0x511390
                scriptSequenceTick();
                scriptActorSpawnRecord(stateAddress(TableId::FireRuinEntranceScriptsEntranceRecord0));
                scriptActorSpawnRecord(stateAddress(TableId::FireRuinEntranceScriptsEntranceRecord1));
                scriptActorSpawnRecord(stateAddress(TableId::FireRuinEntranceScriptsEntranceRecord2));
                scriptActorSpawnRecord(stateAddress(TableId::FireRuinEntranceScriptsEntranceRecord3));
                spawnRecords(stateAddress(TableId::FireRuinEntranceScriptsEntranceSpawns), 3);
            },
            0x16);
        return;
    case CutsceneState::Steps: runSteps(entranceStep); return; // exe 0x52d020
    case CutsceneState::WrapUp:                                // exe 0x52d080
        if (!wrapUpReady()) return;
        wrapUpStart();
        spawnRecords(stateAddress(TableId::FireRuinEntranceScriptsEntranceSpawns), 6);
        spawnRecords(stateAddress(TableId::FireRuinEntranceScriptsEntranceEndSpawns), 3);
        playerBackAt(0xfc000000, 0, 0xa000000, 0);
        soundPlayFlatForced(kRuinMusic); // exe 0x5d5260
        scriptEndBlock();                // exe 0x5116a0
        return;
    default: notPortedAt(kStoryPort, "script 0x40 state", s.state);
    }
}

// ---- script 0x41 (exe 0x52d5a0, camera 0x8a85ac, steps 0x8a86f0) ---------------------------------------------------
// ST27 sub 8 at flag 0x4c, the escape's end: the camera shaking, the script's first actor sent on (sub-state 1), three
// embers, story flag 0x683, the gate's cells opened and the third actor to its state 2; the player placed per step,
// the face tracks 0x8a8754 / 0x8a87ac and the voice 1. The end: records 0x8a8548 x 2 / 0x8a8570 x 3, the player at
// (-0x400, 0, 0x400) yaw 0, the look pitch level, the gate open, the ruin's music.
constexpr RoutineId kGateShake = RoutineId::FireRuinEntranceScriptsGateShake, kGateOpen = RoutineId::FireRuinEntranceScriptsGateOpen, kGatePlaceA = RoutineId::FireRuinEntranceScriptsGatePlaceA, kGateLook = RoutineId::FireRuinEntranceScriptsGateLook, kGatePlaceB = RoutineId::FireRuinEntranceScriptsGatePlaceB, kGateTalk = RoutineId::FireRuinEntranceScriptsGateTalk;
constexpr uint8_t kAnimGateLook = 0x83, kAnimGateLeave = 0x84;
constexpr uint8_t kGateEmber = 0xf;
constexpr int16_t kGateEmberLife = 0x46;
constexpr uint32_t kGateFlag = 0x683;
const DataTable<RecordTable<ShortPoint, 3, 8>> gateEmberPoints{TableId::FireRuinEntranceScriptsGateEmberPoints};

// exe 0x52de00(point): an ember (variant 0xf, sweep group 2) at the point, lifetime 0x46
void gateEmber(const ShortPoint& point) {
    if (const uint32_t slot = ember(kGateEmber, point.x, point.y, point.z, true)) emberParams(slot, kGateEmberLife);
}
// the gate's two cells (each switched twice, as the exe does)
void openGate() {
    switchGridCellVariant(0x3d, 0x3c, 1); // exe 0x58d5f0(cell, 1)
    switchGridCellVariant(0x3d, 0x3c, 1);
    switchGridCellVariant(0x3e, 0x3d, 1);
    switchGridCellVariant(0x3e, 0x3d, 1);
}
void gateStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    KeyframeTracks& faces = s.faceTracks;
    const int32_t t = tick();
    switch (routine.id()) {
    case kGateShake:
        if (t == 0xa) {
            ActorHeader& cast = (*s.actors[0]);
            cast.subState = 1;
            cast.step = 0;
        } else if (t == 0x69) {
            cameraShake(1, 0x1000, 0); // exe 0x40ea80
            gateEmber(gateEmberPoints[0]);
            gateEmber(gateEmberPoints[1]);
            gateEmber(gateEmberPoints[2]);
        } else if (t == 0x73) {
            setStoryFlag(kGateFlag);
        } else if (t == 0x78) {
            cameraShake(1, 0, 0);
        } else if (t == 0xa0) {
            scriptSequenceSkip(1);
        }
        return;
    case kGateOpen:
        if (t != 1) return;
        openGate();
        (*s.actors[2]).state = 2;
        return;
    case kGatePlaceA:
        if (s.subState == 0) {
            setPlayerPos(0xfb000000, 0, 0x3600000, 0xe80);
            nextPhase();
        }
        skipAt(0xf5);
        return;
    case kGateLook:
        if (t == 0xa) {
            playerAnim(kAnimGateLook);
            faces.second.select(1, 0);
        }
        if (t == 0x1e) faces.second.select(0, 0);
        return;
    case kGatePlaceB:
        if (s.subState == 0) {
            player().pos.x = int32_t(0xfa800000);
            player().rotation.y = 0xc00;
            nextPhase();
        }
        skipAt(0x96);
        return;
    case kGateTalk:
        if (t == 0x64) {
            faces.first.select(1, 0);
        } else if (t == 0x78) {
            faces.first.select(2, 0);
            faces.second.select(2, 0);
        } else if (t == 0xde) {
            faces.second.select(3, 0);
        } else if (t == 0xfa) {
            faces.first.select(3, 0);
            faces.second.select(0, 0);
        } else if (t == 0x156) {
            playerAnim(kAnimGateLeave);
        }
        return;
    case kStepIdle: return;
    default: notPortedAt(kStoryPort, "script 0x41 step", routine.pointerValue());
    }
}
void gateScript() { // exe 0x52d5a0
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x52d5c0
        opening(
            [] {
                placePlayer(0xfc000000, 0, 0x4000000, 0);
                playerAnim(kAnimStand);
                facesInit(stateAddress(TableId::FireRuinEntranceScriptsGateFirstTracks), stateAddress(TableId::FireRuinEntranceScriptsGateSecondTracks));
                scriptSequenceStart(stateAddress(TableId::FireRuinEntranceScriptsGateCamera), stateAddress(TableId::FireRuinEntranceScriptsGateSteps));
                scriptSequenceTick();
                cameraShake(1, 0x200, 0);
                scriptActorSpawnRecord(stateAddress(TableId::FireRuinEntranceScriptsGateRecord0));
                scriptActorSpawnRecord(stateAddress(TableId::FireRuinEntranceScriptsGateRecord1));
                scriptActorSpawnRecord(stateAddress(TableId::FireRuinEntranceScriptsGateRecord2));
                scriptActorSpawnRecord(stateAddress(TableId::FireRuinEntranceScriptsGateRecord3));
                scriptActorSpawnRecord(stateAddress(TableId::FireRuinEntranceScriptsGateRecord4));
                scriptActorSpawnRecord(stateAddress(TableId::FireRuinEntranceScriptsGateRecord5));
            },
            1);
        return;
    case CutsceneState::Steps: runSteps(gateStep); return; // exe 0x52d020
    case CutsceneState::WrapUp:                            // exe 0x52d750
        if (!wrapUpReady()) return;
        wrapUpStart();
        spawnRecords(stateAddress(TableId::FireRuinEntranceScriptsGateEndSpawns), 2);
        spawnRecords(stateAddress(TableId::FireRuinEntranceScriptsGateEndMoreSpawns), 3);
        playerBackAt(0xfc000000, 0, 0x4000000, 0);
        cameraState().lookPitch = 0;
        openGate();
        soundPlayFlatForced(kRuinMusic);
        scriptEndBlock();
        return;
    default: notPortedAt(kStoryPort, "script 0x41 state", s.state);
    }
}

// ---- script 0x42 (exe 0x52de50, camera 0x8a8864, steps 0x8a89a0) ---------------------------------------------------
// ST28 sub 5, the first visit (unskippable until its voice plays): the DAT 0xd2 loaded once the music is done, Megaman
// walks in from (-0x6d9, 0x10, 0x200) yaw 0xc00 (animation 0x95), the voice 5, then the stage bank's other buffer and
// his animation paused / resumed over the face tracks 0x8a89e8 / 0x8a8a14. The end: records 0x8a8850 / 0x8a8824 x 2,
// the player at (-0x500, 0x10, 0x200) yaw 0xc00, the depths' music.
constexpr RoutineId kDepthsWalkIn = RoutineId::FireRuinEntranceScriptsDepthsWalkIn, kDepthsPause = RoutineId::FireRuinEntranceScriptsDepthsPause, kDepthsBlink = RoutineId::FireRuinEntranceScriptsDepthsBlink;
constexpr uint8_t kAnimDepthsWalk = 0x95, kAnimDepthsLook = 0x82;
constexpr uint16_t kDepthsMusic = 0x3f;
constexpr DatFile kDepthsDat{"ST2801.DAT"};
// the depths' walk-in's own phases (after the walk-in's), and its file load's (the step phase)
enum class DepthsWalk : uint8_t { Wait = 4, WaitFile = 5, Voice = 6, WaitVoice = 7 };
enum class DepthsFile : uint8_t { Request = 0, Loading = 1, Loaded = 2 };

void depthsStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    KeyframeTracks& faces = s.faceTracks;
    const int32_t t = tick();
    switch (routine.id()) {
    case kDepthsWalkIn:
        footstep(kAnimDepthsWalk);
        switch (DepthsFile(s.stepPhase)) {
        case DepthsFile::Request:
            if (soundDone(kMusicFades)) {
                datLoadRequest(kDepthsDat); // exe 0x40efe0
                nextStepPhase();
            }
            break;
        case DepthsFile::Loading:
            if (!fileLoading() && display().drawBuffer == 1) nextStepPhase();
            break;
        default: break;
        }
        if (!walkIn(0x11, kAnimDepthsWalk)) return;
        switch (DepthsWalk(s.subState)) {
        case DepthsWalk::Wait:
            if (t == 0x50) nextPhase();
            return;
        case DepthsWalk::WaitFile:
            if (DepthsFile(s.stepPhase) == DepthsFile::Loaded) nextPhase();
            return;
        case DepthsWalk::Voice:
            if (streamBusy()) return;
            voiceClipPlay(5);
            nextPhase();
            return;
        case DepthsWalk::WaitVoice:
            if (!voiceClipPlaying()) return;
            stage().bankBuffer ^= 1u;
            stageBankWalk(); // exe 0x5f3200
            playerAnim(0);
            s.flags &= uint8_t(~kScriptUnskippable);
            scriptSequenceSkip(1);
            s.subState = 0;
            s.stepPhase = 0;
            return;
        default: return;
        }
    case kDepthsPause:
        if (t == 0x78) {
            playerAnim(kAnimDepthsLook);
            player().animPaused = 1;
            scriptSequenceSkip(1);
        }
        return;
    case kDepthsBlink:
        if (t == 0x78) {
            player().animPaused = 0;
        } else if (t == 0x80) {
            player().animPaused = 1;
        } else if (t == 0x8a) {
            faces.first.select(1, 0);
            faces.second.select(1, 0);
        } else if (t == 0x9c) {
            faces.second.select(0, 0);
        }
        return;
    case kStepIdle: return;
    default: notPortedAt(kStoryPort, "script 0x42 step", routine.pointerValue());
    }
}
enum class DepthsSetUp : uint8_t { Start = 0, TakeOver = 1 };
void depthsScript() { // exe 0x52de50
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x52de70
        switch (DepthsSetUp(s.subState)) {
        case DepthsSetUp::Start:
            setUpStart(false);
            s.subState = uint8_t(DepthsSetUp::TakeOver);
            s.flags |= kScriptUnskippable;
            [[fallthrough]];
        case DepthsSetUp::TakeOver:
            if (playerToScriptIfFree() != 0) return;
            actorsSweep(kSweepActors, kSweepFree);
            placePlayer(0xf9270000, 0x100000, 0x2000000, 0xc00);
            facesInit(stateAddress(TableId::FireRuinEntranceScriptsDepthsFirstTracks), stateAddress(TableId::FireRuinEntranceScriptsDepthsSecondTracks));
            scriptSequenceStart(stateAddress(TableId::FireRuinEntranceScriptsDepthsCamera), stateAddress(TableId::FireRuinEntranceScriptsDepthsSteps));
            scriptSequenceTick();
            s.state = uint8_t(CutsceneState::Steps);
            s.subState = 0;
            s.stepPhase = 0;
            return;
        default: return;
        }
    case CutsceneState::Steps: runSteps(depthsStep); return; // exe 0x52d020
    case CutsceneState::WrapUp:                              // exe 0x52df50
        if (!wrapUpReady()) return;
        wrapUpStart();
        spawnRecords(stateAddress(TableId::FireRuinEntranceScriptsDepthsEndSpawns), 1);
        spawnRecords(stateAddress(TableId::FireRuinEntranceScriptsDepthsEndMoreSpawns), 2);
        playerBackAt(0xfb000000, 0x100000, 0x2000000, 0xc00);
        player().animPaused = 0;
        soundPlayFlatForced(kDepthsMusic);
        scriptEndBlock();
        return;
    default: notPortedAt(kStoryPort, "script 0x42 state", s.state);
    }
}

// ---- script 0x43 (exe 0x52e2e0, camera 0x8a8a8c, steps 0x8a8b90) ---------------------------------------------------
// ST28 sub 5 at flag 0x4e, the falling rubble: fade 0x11 and the voice idle, Megaman at (-0x3d9, 0x10, 0x200) yaw
// 0xc00 with the face tracks 0x8a8bf4 / 0x8a8c1c, the voice 0xa, then the sequence (fade 1) and three flames; blasts
// fall around him, he walks in (animation 0x83) and turns, the script's actors 1 / 2 placed. The end (its own): the
// voice faded, DAT 0xd3, the stage bank's other buffer, records 0x8a8a64 x 2, the player at (0, 0x10, 0x200) yaw
// 0xc00, the look pitch level, the ruin's music.
constexpr RoutineId kRubbleFall = RoutineId::FireRuinEntranceScriptsRubbleFall, kRubbleWalkIn = RoutineId::FireRuinEntranceScriptsRubbleWalkIn, kRubbleFaces = RoutineId::FireRuinEntranceScriptsRubbleFaces, kRubbleCast = RoutineId::FireRuinEntranceScriptsRubbleCast, kRubblePlace = RoutineId::FireRuinEntranceScriptsRubblePlace;
constexpr uint8_t kAnimRubbleWalk = 0x83, kAnimRubbleLook = 0x85, kAnimRubbleBrace = 0x86;
constexpr DatFile kRubbleDat{"ST2802.DAT"};
constexpr uint8_t kRubbleFlame = 0x20;
constexpr int16_t kRubbleFlameLife = 0x50;
constexpr int16_t kRubbleFlameYaw = 0x800;
const DataTable<RecordTable<ShortPoint, 3, 8>> rubbleFlamePoints{TableId::FireRuinEntranceScriptsRubbleFlamePoints};
enum class RubbleSetUp : uint8_t { Start = 0, WaitIdle = 1, TakeOver = 2, WaitFiles = 3, Voice = 4, Sequence = 5 };
enum class RubbleWrapUp : uint8_t { FadeVoice = 0, WaitIdle = 1, Load = 2, End = 3 };

// a blast (the global static type 7, sweep group 2) of the falling rubble: its variant
uint32_t rubbleBlast(uint8_t variant) {
    const uint32_t slot = allocActor60(0); // exe 0x5e1300
    if (!slot) return 0;
    StaticActor& blast = staticActorAt(slot);
    blast.flags = kActorLive;
    blast.type = typeByte(GlobalStaticType::Blast);
    blast.variant = variant;
    blast.loopFlags |= kActorSweepGroup2;
    return slot;
}
// the big piece: variant 1 at (0x400, -0x40, 0x200), its size, attack 4
void bigRubble(uint16_t size) {
    const uint32_t slot = rubbleBlast(1);
    if (!slot) return;
    StaticActor& blast = staticActorAt(slot);
    blast.pos.x.whole = 0x400;
    blast.pos.y.whole = -0x40;
    blast.pos.z.whole = 0x200;
    BlastParams params(actorHeaderAt(slot));
    params.size = size;
    params.attack = 4;
}
// exe 0x52e9f0: three flames (spark emitters variant 0x20) at the points, yaw 0x800, lifetime 0x50
void rubbleFlames() {
    for (size_t k = 0; k < 3; ++k) {
        const ShortPoint point = rubbleFlamePoints[k];
        const uint32_t slot = ember(kRubbleFlame, point.x, point.y, point.z, false);
        if (!slot) continue;
        staticActorAt(slot).rotation.y = kRubbleFlameYaw;
        emberParams(slot, kRubbleFlameLife);
    }
}
void rubbleStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    KeyframeTracks& faces = s.faceTracks;
    const int32_t t = tick();
    switch (routine.id()) {
    case kRubbleFall: // every 4th tick before 0x82 a small piece at a random spot; at tick 0x46 a big one
        if (t < 0x82 && (t & 3) == 0) {
            if (const uint32_t slot = rubbleBlast(0)) {
                StaticActor& blast = staticActorAt(slot);
                BlastParams params(actorHeaderAt(slot));
                blast.pos.y.whole = -0x40;
                blast.pos.x.whole = int16_t((rngStep() & 0xff) + 0x300);
                blast.pos.z.whole = int16_t((rngStep() & 0xff) + 0x180);
                params.attack = 0;
                params.size = uint16_t(((rngStep() & 3) + 8) << 4);
            }
        }
        if (tick() == 0x46) bigRubble(0x180);
        return;
    case kRubbleWalkIn: // pieces at ticks 0x14 / 0x32; the walk-in, then the yaw turns -0x10 a tick over 0x65..0x8b
        footstep(kAnimRubbleWalk);
        if (t == 0x14 || t == 0x32) bigRubble(0xc0);
        if (!walkIn(0x21, kAnimRubbleWalk) || !walkInDone()) return;
        if (tick() > 0x64 && tick() < 0x8c) player().rotation.y = int16_t(uint16_t(player().rotation.y) - 0x10u);
        if (tick() == 0x96) {
            scriptSequenceSkip(1);
            s.subState = 0;
        }
        return;
    case kRubbleFaces:
        if (t == 0x14) {
            faces.first.select(1, 0);
            faces.second.select(1, 0);
        }
        return;
    case kRubbleCast: // the script's actors 1 / 2 placed once
        if (s.subState == 0) {
            Actor& first = actorLayoutOf(*s.actors[1]);
            first.pos.x.whole = 0x160;
            first.pos.z.whole = 0x1b0;
            first.rotation.y = 0x900;
            Actor& second = actorLayoutOf(*s.actors[2]);
            second.pos.x.whole = 0x120;
            second.pos.z.whole = 0x260;
            second.rotation.y = 0xc00;
            nextPhase();
        }
        skipAt(0x460);
        return;
    case kRubblePlace:
        if (s.subState == 0) {
            setPlayerPos(0xfc000000, 0x100000, 0x2000000, 0xc00);
            nextPhase();
        }
        if (t == 5) playerAnim(kAnimRubbleLook);
        if (t == 0x23) playerAnim(kAnimRubbleBrace);
        return;
    case kStepIdle: return;
    default: notPortedAt(kStoryPort, "script 0x43 step", routine.pointerValue());
    }
}
void rubbleScript() { // exe 0x52e2e0
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x52e300
        switch (RubbleSetUp(s.subState)) {
        case RubbleSetUp::Start:
            scriptReplayPadOn();
            soundFadeOut(kMusicFades, 0xfff, 0);
            scriptFade(kSkipFade, 0);
            windowCloseAll(WindowClose::Force);
            s.subState = uint8_t(RubbleSetUp::WaitIdle);
            [[fallthrough]];
        case RubbleSetUp::WaitIdle:
            if (voiceClipIdle() && fadeState().busy == 0) s.subState = uint8_t(RubbleSetUp::TakeOver);
            return;
        case RubbleSetUp::TakeOver:
            if (playerToScriptIfFree() != 0) return;
            actorsSweep(kSweepActors, kSweepFree);
            placePlayer(0xfc270000, 0x100000, 0x2000000, 0xc00);
            playerAnim(0);
            facesInit(stateAddress(TableId::FireRuinEntranceScriptsRubbleFirstTracks), stateAddress(TableId::FireRuinEntranceScriptsRubbleSecondTracks));
            s.subState = uint8_t(RubbleSetUp::WaitFiles);
            return;
        case RubbleSetUp::WaitFiles:
            if (!fileLoading()) s.subState = uint8_t(RubbleSetUp::Voice);
            return;
        case RubbleSetUp::Voice:
            if (streamBusy()) return;
            voiceClipPlay(0xa);
            s.subState = uint8_t(RubbleSetUp::Sequence);
            return;
        case RubbleSetUp::Sequence:
            if (!voiceClipPlaying()) return;
            scriptSequenceStart(stateAddress(TableId::FireRuinEntranceScriptsRubbleCamera), stateAddress(TableId::FireRuinEntranceScriptsRubbleSteps));
            scriptSequenceTick();
            scriptFade(kSkippedFade, 0);
            playerAnim(1);
            rubbleFlames();
            s.enterStateKeepingPhases(uint8_t(CutsceneState::Steps));
            return;
        default: return;
        }
    case CutsceneState::Steps: { // exe 0x52e490: the end / the skip enter the wrap-up byte by byte
        const ScriptStep step = scriptStepAt(s.steps.value());
        bool end = step.id == kScriptStepEnd;
        if (!end) {
            rubbleStep(stateRoutine(step.routine));
            if (scriptSkipRequested() && display().drawBuffer != 0) {
                scriptFade(kSkipFade, 0);
                end = true;
            }
        }
        if (end) s.enterStateBytes(uint8_t(CutsceneState::WrapUp));
        scriptSequenceTick();
        faceTracksTick(s.faceTracks);
        return;
    }
    case CutsceneState::WrapUp: // exe 0x52e500
        switch (RubbleWrapUp(s.subState)) {
        case RubbleWrapUp::FadeVoice:
            playerAnim(0);
            if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade);
            scriptFade(kSkipFade, 0);
            s.subState = uint8_t(RubbleWrapUp::WaitIdle);
            return;
        case RubbleWrapUp::WaitIdle:
            if (voiceClipIdle() && fadeState().busy == 0 && soundDone(kMusicFades)) {
                actorsSweep(kSweepActors, kSweepFree);
                s.subState = uint8_t(RubbleWrapUp::Load);
            }
            return;
        case RubbleWrapUp::Load:
            datLoadRequest(kRubbleDat);
            s.subState = uint8_t(RubbleWrapUp::End);
            return;
        case RubbleWrapUp::End:
            if (fileLoading()) return;
            scriptFade(kSkippedFade, 0);
            stage().bankBuffer ^= 1u;
            stageBankWalk();
            spawnRecords(stateAddress(TableId::FireRuinEntranceScriptsRubbleEndSpawns), 2);
            playerBackAt(0, 0x100000, 0x2000000, 0xc00);
            cameraState().lookPitch = 0;
            soundPlayFlatForced(kRuinMusic);
            scriptEndBlock();
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x43 state", s.state);
    }
}


} // namespace

bool fireRuinEntranceScriptsPorted(RoutineId handler) {
    return handler == RoutineId::FireRuinEntranceScript || handler == RoutineId::FireRuinGateScript || handler == RoutineId::FireRuinDepthsScript ||
           handler == RoutineId::FireRuinRubbleScript;
}
bool fireRuinEntranceScriptsTick(const PlayerEnv&, RoutineId handler) {
    if (!fireRuinEntranceScriptsPorted(handler)) return false;
    switch (handler) {
    case RoutineId::FireRuinEntranceScript: entranceScript(); break;
    case RoutineId::FireRuinGateScript: gateScript(); break;
    case RoutineId::FireRuinDepthsScript: depthsScript(); break;
    case RoutineId::FireRuinRubbleScript: rubbleScript(); break;
    default: break;
    }
    return true;
}

} // namespace od::port
