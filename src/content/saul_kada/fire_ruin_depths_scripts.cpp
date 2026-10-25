// The fire ruin's story-13 scripts of ST28 (their rooms: exe 0x583390 / 0x583080 in content/saul_kada/fire_ruin_rooms):
// script 0x44 (exe 0x52ea50), 0x45 (0x52ef50), 0x46 (0x52f320), 0x47 (0x52f5e0); hypothesis names. Each handler
// dispatches on the script's state (its table): the set-up, the steps runner (the step routines dispatched by their exe
// address), the wrap-up.
#include "content/saul_kada/fire_ruin_depths_scripts.hpp"

#include "engine/routine_ids.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/audio_logic.hpp"
#include "game/camera_state.hpp"
#include "game/display.hpp"
#include "game/fade_state.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/message.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/rewards.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/spawn.hpp"
#include "game/stage_files.hpp"
#include "game/story.hpp"
#include "game/transition.hpp"

namespace od::port {

namespace {

constexpr uint8_t kSkipFade = 0x11, kSkippedFade = 1;
constexpr uint32_t kVoiceFade = 8;
// the set-up's steps (the script's sub-state)
enum class SetUp : uint8_t { Start = 0, TakeOver = 1, WaitFiles = 2, Voice = 3, WaitVoice = 4, Sequence = 5 };
enum class WrapUp : uint8_t { FadeVoice = 0, WaitVoice = 1, End = 2 };

// ---- the shared parts ------------------------------------------------------------------------------------------
int32_t tick() { return scriptState().stepTick; }
bool fileLoading() { return (fileLoads().loading & kFileLoading) != 0; }
bool streamBusy() { return (fileLoads().loading & kStreamBusy) != 0; }
// the player placed for a cutscene (the exe's inline block): his move angles and idle faces cleared, his position, yaw
void placePlayer(uint32_t x, uint32_t y, uint32_t z, int16_t yaw) {
    Player& p = player();
    p.clearDirectionOffsetWords();
    p.idleFaces[0] = 0;
    p.idleFaces[1] = 0;
    p.pos.x = int32_t(x);
    p.pos.y = int32_t(y);
    p.pos.z = int32_t(z);
    p.rotation.y = yaw;
}
// the player back at the end (on the ground) at his place
void playerBackAt(uint32_t x, uint32_t y, uint32_t z, int16_t yaw) {
    playerBackToPlay();   // exe 0x5723c0
    scriptReplayPadOff(); // exe 0x511890
    Player& p = player();
    p.airborne = 0;
    p.pos.x = int32_t(x);
    p.pos.y = int32_t(y);
    p.pos.z = int32_t(z);
    p.rotation.y = yaw;
}
void playerAnim(uint8_t anim) { playerScriptAnimId(anim, 0, 1); } // exe 0x5723d0(anim, 0, 1)
uint8_t playerAnimShown() { return player().body.anim; }
uint8_t playerKeyFrame() { return uint8_t(player().body.key.frame); }
// the player walking backwards (exe 0x5e36f0 along his yaw)
void walkBack(int speed) { moveAlongYaw(player().pos, player().rotation.y, 0, 0, speed); }
void nextPhase() { scriptState().subState += 1u; }
// the tail of a step routine: at the tick one step ahead (exe 0x5115c0(1)), the sub-state cleared
void skipAt(int32_t at) {
    if (tick() != at) return;
    scriptSequenceSkip(1);
    scriptState().subState = 0;
}
// the head the wrap-ups share (exe 0x52ebb0 / 0x52f0a0 / 0x52f770): the voice faded out, then idle with no fade
// running; true once there
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
// exe 0x52eb60 (scripts 0x44 / 0x47) and exe 0x52d020 (0x45, with the face tracks): the step's routine (run whatever
// it is), the skip -> fade 0x11 and the wrap-up; the list's end -> the wrap-up; the sequence's tick
void runSteps(ScriptStepRoutines routines, bool faces) {
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
    scriptSequenceTick(); // exe 0x5114d0
    if (faces) faceTracksTick(s.faceTracks); // exe 0x5e3410
}
// the set-up of scripts 0x44 / 0x45 once the player is the script's: the actors swept, the player placed, the sequence
// (the extra set-up between)
template <class Extra> bool takeOver(uint32_t x, uint32_t y, uint32_t z, int16_t yaw, uint32_t camera, uint32_t steps,
                                     Extra extra) {
    if (playerToScriptIfFree() != 0) return false; // exe 0x572300
    actorsSweep(kSweepActors, kSweepFree);          // exe 0x5e05d0
    placePlayer(x, y, z, yaw);
    extra();
    scriptSequenceStart(camera, steps); // exe 0x511390
    scriptSequenceTick();
    return true;
}

// ---- script 0x44 (exe 0x52ea50, states [0x8a8ee8], camera 0x8a8d24, steps 0x8a8ea8) -------------------------------------
// ST28 sub 3, the first visit: Megaman walks backwards from (0x200, -0x40, -0xe89) yaw 0x800 to the edge, staggers
// (animations 0x95 / 0x92 / 0x96 / 0x83 with footstep sounds 0x91 / 0x90), turns to yaw 0x900; the script's actor 6
// (record 0x8a8ccc) is counted down on its parameter and sent to its state 2; the voice 0x13 plays over it. The end:
// records 0x8a8c50 x 5 / 0x8a8cf8 / 0x8a8d10, the player at (0x200, -0x40, -0xa00) yaw 0x800.
constexpr RoutineId kEdgeWalkBack = RoutineId::FireRuinDepthsScriptsEdgeWalkBack,
                    kEdgeStagger = RoutineId::FireRuinDepthsScriptsEdgeStagger,
                    kEdgeStepBack = RoutineId::FireRuinDepthsScriptsEdgeStepBack,
                    kEdgeTurn = RoutineId::FireRuinDepthsScriptsEdgeTurn,
                   kEdgeCountdown = RoutineId::FireRuinDepthsScriptsEdgeCountdown, kStepIdle = RoutineId::EmptyRoutine;
constexpr uint8_t kAnimWalkBack = 0x95, kAnimStagger = 0x92, kAnimStepBack = 0x96, kAnimLookDown = 0x83;
constexpr uint16_t kStepSound = 0x91, kScuffSound = 0x90;
constexpr size_t kCountdownActor = 6;
// The script's actor 6's parameter: the countdown that sends it to its state 2
struct CountdownParams {
    explicit CountdownParams(ActorHeader& actor) : countdown(actor.params, 0) {}
    BytesField<int16_t, 4> countdown; // +0xc
};

// exe 0x52ec90 / 0x52f170: the backward walk of scripts 0x44 / 0x45 (its footsteps on the walk animation's keys)
void backwardWalk(uint8_t walkAnim) {
    if (playerAnimShown() == walkAnim) {
        const uint8_t frame = playerKeyFrame();
        if (frame == 2 || frame == 0xd || frame == 0x16) soundPlayAtPlayer(kStepSound); // exe 0x5d53e0
    }
    switch (scriptState().subState) { // [0x52ed94]
    case 0:
        playerAnim(1); // exe 0x5723d0
        nextPhase();
        return;
    case 1:
        walkBack(-0x200);
        if (tick() == 0x21) {
            playerAnim(walkAnim);
            nextPhase();
        }
        return;
    case 2:
        walkBack(-0x200);
        if (playerKeyFrame() == 3) nextPhase();
        return;
    case 3:
        walkBack(-0x76);
        if (playerKeyFrame() == 0x11) nextPhase();
        return;
    case 4: skipAt(0x46); return;
    default: return;
    }
}
void edgeStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    switch (routine.id()) {
    case kEdgeWalkBack: backwardWalk(kAnimWalkBack); return;
    case kEdgeStagger:
        if (s.subState == 0) {
            playerAnim(kAnimStagger);
            nextPhase();
        }
        skipAt(0x28);
        return;
    case kEdgeStepBack: {
        uint8_t frame = playerKeyFrame();
        if (playerAnimShown() == kAnimStepBack && (frame == 4 || frame == 0x16)) {
            soundPlayAtPlayer(kScuffSound);
            frame = playerKeyFrame();
        }
        if (playerAnimShown() == kAnimLookDown && (frame == 4 || frame == 0x10)) soundPlayAtPlayer(kScuffSound);
        switch (s.subState) {
        case 0:
            playerAnim(kAnimStepBack);
            nextPhase();
            return;
        case 1:
            walkBack(-0x50);
            if (tick() == 0x40) {
                playerAnim(kAnimLookDown);
                nextPhase();
            }
            return;
        case 2: skipAt(0x5a); return;
        default: return;
        }
    }
    case kEdgeTurn:
        if (s.subState == 0) {
            player().rotation.y = 0x900;
            nextPhase();
        }
        skipAt(0x5c);
        return;
    case kEdgeCountdown:
        switch (s.subState) {
        case 0:
            if (tick() == 0x3c) nextPhase();
            return;
        case 1: {
            const uint32_t cast = slotIdentityWord(s.actors[kCountdownActor]);
            CountdownParams params(actorHeaderAt(cast));
            --params.countdown;
            if (params.countdown < 1) {
                actorHeaderAt(cast).state = 2;
                nextPhase();
            }
            return;
        }
        default: return;
        }
    case kStepIdle: return;
    default: notPortedAt(kStoryPort, "script 0x44 step", routine.pointerValue());
    }
}
// the set-up shared by scripts 0x44 / 0x45: the replay pad, the take-over, the files / the stream, the voice
template <class TakeOver> void pitSetUp(TakeOver takeOverNow, bool waitMusic, uint32_t voice) {
    ScriptState& s = scriptState();
    switch (SetUp(s.subState)) {
    case SetUp::Start:
        scriptReplayPadOn(); // exe 0x5117f0
        s.subState = uint8_t(SetUp::TakeOver);
        [[fallthrough]];
    case SetUp::TakeOver:
        if (!takeOverNow()) return;
        s.subState = uint8_t(SetUp::WaitFiles);
        return;
    case SetUp::WaitFiles:
        if ((!waitMusic || soundDone(kMusicFades)) && !fileLoading()) s.subState = uint8_t(SetUp::Voice); // 0x5d5900
        return;
    case SetUp::Voice:
        if (streamBusy()) return;
        voiceClipPlay(voice); // exe 0x601050
        s.subState = uint8_t(SetUp::WaitVoice);
        return;
    case SetUp::WaitVoice:
        if (voiceClipPlaying()) s.enterStateKeepingPhases(uint8_t(CutsceneState::Steps));
        return;
    default: return;
    }
}
void edgeScript() { // exe 0x52ea50
    switch (scriptState().cutsceneState()) {
    case CutsceneState::SetUp: // 0x52ea70 ([0x52eb44])
        pitSetUp(
            [] {
                return takeOver(0x2000000, 0xffc00000, 0xf1770000, 0x800, stateAddress(TableId::FireRuinDepthsScriptsEdgeCamera), stateAddress(TableId::FireRuinDepthsScriptsEdgeSteps), [] {});
            },
            false, 0x13);
        return;
    case CutsceneState::Steps: runSteps(edgeStep, false); return; // exe 0x52eb60
    case CutsceneState::WrapUp:                                   // exe 0x52ebb0
        if (!wrapUpReady()) return;
        wrapUpStart();
        spawnRecords(stateAddress(TableId::FireRuinDepthsScriptsEdgeSpawns), 5);
        spawnRecords(stateAddress(TableId::FireRuinDepthsScriptsEdgeMoreSpawns), 1);
        spawnRecords(stateAddress(TableId::FireRuinDepthsScriptsEdgeLastSpawns), 1);
        playerBackAt(0x2000000, 0xffc00000, 0xf6000000, 0x800);
        scriptEndBlock(); // exe 0x5116a0
        return;
    default: notPortedAt(kStoryPort, "script 0x44 state", scriptState().state);
    }
}

// ---- script 0x45 (exe 0x52ef50, states [0x8a90dc], camera 0x8a8f74, steps 0x8a9070) -------------------------------------
// ST28 sub 3 at flag 0x4e: the music channels done, the same backward walk (animations 0x81 / 0x82, footsteps 0x91)
// from (0x200, -0x300, 0xf59) yaw 0 with the face tracks 0x8a90a8 / 0x8a90d4 (sequence 1 at tick 0xf, 0 at 0x28) and
// the voice 0xd. The end: records 0x8a8ef8 x 5 / 0x8a8f60, the player at (0x200, -0x300, 0xb00) yaw 0.
constexpr RoutineId kReturnWalkBack = RoutineId::FireRuinDepthsScriptsReturnWalkBack,
                    kReturnStop = RoutineId::FireRuinDepthsScriptsReturnStop,
                    kReturnFaces = RoutineId::FireRuinDepthsScriptsReturnFaces;
constexpr uint8_t kAnimReturnWalk = 0x81, kAnimReturnStop = 0x82;

void returnStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    switch (routine.id()) {
    case kReturnWalkBack: backwardWalk(kAnimReturnWalk); return;
    case kReturnStop:
        if (s.subState == 0) {
            playerAnim(kAnimReturnStop);
            nextPhase();
        }
        skipAt(0x3c);
        return;
    case kReturnFaces:
        if (tick() == 0xf) {
            s.faceTracks.first.select(1, 0);  // exe 0x5e33d0
            s.faceTracks.second.select(1, 0); // exe 0x5e33f0
        }
        if (tick() == 0x28) {
            s.faceTracks.first.select(0, 0);
            s.faceTracks.second.select(0, 0);
        }
        return;
    case kStepIdle: return;
    default: notPortedAt(kStoryPort, "script 0x45 step", routine.pointerValue());
    }
}
void returnScript() { // exe 0x52ef50
    switch (scriptState().cutsceneState()) {
    case CutsceneState::SetUp: // 0x52ef70 ([0x52f080])
        pitSetUp(
            [] {
                return takeOver(0x2000000, 0xfd000000, 0xf590000, 0, stateAddress(TableId::FireRuinDepthsScriptsReturnCamera), stateAddress(TableId::FireRuinDepthsScriptsReturnSteps), [] {
                    KeyframeTracks& faces = scriptState().faceTracks;
                    faces.init(stateAddress(TableId::FireRuinDepthsScriptsReturnFirstTracks), stateAddress(TableId::FireRuinDepthsScriptsReturnSecondTracks)); // exe 0x5e33a0
                    faces.first.select(0, 0);
                    faces.second.select(0, 0);
                });
            },
            true, 0xd);
        return;
    case CutsceneState::Steps: runSteps(returnStep, true); return; // exe 0x52d020
    case CutsceneState::WrapUp:                                    // exe 0x52f0a0
        if (!wrapUpReady()) return;
        wrapUpStart();
        spawnRecords(stateAddress(TableId::FireRuinDepthsScriptsReturnSpawns), 5);
        spawnRecords(stateAddress(TableId::FireRuinDepthsScriptsReturnMoreSpawns), 1);
        playerBackAt(0x2000000, 0xfd000000, 0xb000000, 0);
        scriptEndBlock();
        return;
    default: notPortedAt(kStoryPort, "script 0x45 state", scriptState().state);
    }
}

// ---- script 0x46 (exe 0x52f320, states [0x8a9270], camera 0x8a917c, steps 0x8a9248) -------------------------------------
// ST28 sub 0 at flag 0x53 (the boss beaten): the windows closed, the player at (0x300, -0x40, -0x300) yaw 0x800, the
// voice 0xe over the sequence; the script's first actor signalled at tick 0x28, the replay pad holds forward from tick
// 0x1e to 0x30. The end (skip: the skip's wipe): the cast 0x8a9100 / 0x8a9118 / 0x8a912c / 0x8a9140 / 0x8a9154 /
// 0x8a9168, the player at (0x300, -0x40, -0xe0) yaw 0x800, the tool-kit balance += 1500.
constexpr RoutineId kBeatenSignal = RoutineId::FireRuinDepthsScriptsBeatenSignal,
                    kBeatenWalk = RoutineId::FireRuinDepthsScriptsBeatenWalk,
                    kBeatenSkip = RoutineId::FireRuinDepthsScriptsBeatenSkip;
constexpr int kBeatenBalance = 0x5dc;
enum class BeatenSetUp : uint8_t { Start = 0, Place = 1, Sequence = 2, WaitVoice = 3 };

void beatenStep(const RoutineRef& routine) {
    switch (routine.id()) {
    case kBeatenSignal:
        if (tick() == 0x28) (*scriptState().actors[0]).params[0] = 1;
        return;
    case kBeatenWalk:
        if (tick() == 0x1e) replayPad().held = player().button(PlayerButton::Forward);
        else if (tick() == 0x30) replayPad().held = 0;
        return;
    case kBeatenSkip:
        if (tick() == 0x96) scriptSequenceSkip(1);
        return;
    case kStepIdle: return;
    default: notPortedAt(kStoryPort, "script 0x46 step", routine.pointerValue());
    }
}
void beatenScript() { // exe 0x52f320
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // 0x52f340 ([0x52f408])
        switch (BeatenSetUp(s.subState)) {
        case BeatenSetUp::Start:
            scriptPlayerReset();                // exe 0x511840
            windowCloseAll(WindowClose::Force); // exe 0x5577d0
            s.subState = uint8_t(BeatenSetUp::Place);
            s.flags |= kScriptUnskippable;
            return;
        case BeatenSetUp::Place:
            actorsSweep(kSweepActors, kSweepFree);
            placePlayer(0x3000000, 0xffc00000, 0xfd000000, 0x800);
            s.subState = uint8_t(BeatenSetUp::Sequence);
            return;
        case BeatenSetUp::Sequence:
            voiceClipPlay(0xe);
            scriptSequenceStart(stateAddress(TableId::FireRuinDepthsScriptsBeatenCamera), stateAddress(TableId::FireRuinDepthsScriptsBeatenSteps));
            scriptSequenceTick();
            s.subState = uint8_t(BeatenSetUp::WaitVoice);
            return;
        case BeatenSetUp::WaitVoice:
            if (voiceClipPlaying()) {
                s.enterStateKeepingPhases(uint8_t(CutsceneState::Steps));
                s.flags &= uint8_t(~kScriptUnskippable);
            }
            return;
        default: return;
        }
    case CutsceneState::Steps: scriptStepsStateNoFaces(beatenStep); return; // exe 0x52f420
    case CutsceneState::WrapUp: // 0x52f470 (its second phase waits only for the fade, not the voice)
        switch (WrapUp(s.subState)) {
        case WrapUp::FadeVoice:
            if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade);
            nextPhase();
            return;
        case WrapUp::WaitVoice:
            if (fadeState().busy == 0) nextPhase();
            return;
        case WrapUp::End:
            wrapUpStart();
            scriptActorSpawnRecord(stateAddress(TableId::FireRuinDepthsScriptsBeatenRecord0));
            scriptActorSpawnRecord(stateAddress(TableId::FireRuinDepthsScriptsBeatenRecord1));
            scriptActorSpawnRecord(stateAddress(TableId::FireRuinDepthsScriptsBeatenRecord2));
            scriptActorSpawnRecord(stateAddress(TableId::FireRuinDepthsScriptsBeatenRecord3));
            scriptActorSpawnRecord(stateAddress(TableId::FireRuinDepthsScriptsBeatenRecord4));
            scriptActorSpawnRecord(stateAddress(TableId::FireRuinDepthsScriptsBeatenRecord5));
            playerBackAt(0x3000000, 0xffc00000, 0xff200000, 0x800);
            toolKitBalanceAdd(kBeatenBalance); // exe 0x5e5260
            scriptEndBlock();
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x46 state", s.state);
    }
}

// ---- script 0x47 (exe 0x52f5e0, states [0x8a9450], camera 0x8a9314, steps 0x8a9418) -------------------------------------
// ST28 sub 0 at flag 0x54: fade 0x11, the windows closed, the voice idle; the player at (0x300, -0x40, -0x500) yaw 0
// with the cast 0x8a92b0 / 0x8a92c4 / 0x8a92d8 / 0x8a92ec; the voice 0x12, then the sequence (fade 1); skippable once
// no fade runs, Megaman's animation 0x50. The end: records 0x8a9298 / 0x8a92b0 x 4 / 0x8a9300, the player at (0x300,
// -0x40, -0x500) yaw 0, the music group 4 dipped 0x333, the camera's look pitch level.
constexpr RoutineId kLeaveWait = RoutineId::FireRuinDepthsScriptsLeaveWait,
                    kLeavePose = RoutineId::FireRuinDepthsScriptsLeavePose;
constexpr uint8_t kAnimLeavePose = 0x50;
enum class LeaveSetUp : uint8_t { Start = 0, WaitIdle = 1, TakeOver = 2, WaitFiles = 3, Voice = 4, Sequence = 5 };

void leaveStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    switch (routine.id()) {
    case kLeaveWait:
        if (s.subState == 0 && fadeState().busy == 0) {
            s.subState = 1;
            s.flags &= uint8_t(~kScriptUnskippable);
        }
        skipAt(0x55);
        return;
    case kLeavePose:
        if (s.subState == 0) {
            playerAnim(kAnimLeavePose);
            nextPhase();
        }
        skipAt(0x5a);
        return;
    case kStepIdle: return;
    default: notPortedAt(kStoryPort, "script 0x47 step", routine.pointerValue());
    }
}
void leaveScript() { // exe 0x52f5e0
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // 0x52f600 ([0x52f74c])
        switch (LeaveSetUp(s.subState)) {
        case LeaveSetUp::Start:
            scriptReplayPadOn();
            scriptFade(kSkipFade, 0);
            windowCloseAll(WindowClose::Force);
            s.subState = uint8_t(LeaveSetUp::WaitIdle);
            s.flags |= kScriptUnskippable;
            return;
        case LeaveSetUp::WaitIdle:
            if (voiceClipIdle() && fadeState().busy == 0) s.subState = uint8_t(LeaveSetUp::TakeOver);
            return;
        case LeaveSetUp::TakeOver:
            if (playerToScriptIfFree() != 0) return;
            actorsSweep(kSweepActors, kSweepFree);
            placePlayer(0x3000000, 0xffc00000, 0xfb000000, 0);
            scriptActorSpawnRecord(stateAddress(TableId::FireRuinDepthsScriptsLeaveRecords));
            scriptActorSpawnRecord(stateAddress(TableId::FireRuinDepthsScriptsLeaveRecord1));
            scriptActorSpawnRecord(stateAddress(TableId::FireRuinDepthsScriptsLeaveRecord2));
            scriptActorSpawnRecord(stateAddress(TableId::FireRuinDepthsScriptsLeaveRecord3));
            s.subState = uint8_t(LeaveSetUp::WaitFiles);
            return;
        case LeaveSetUp::WaitFiles:
            if (!fileLoading()) s.subState = uint8_t(LeaveSetUp::Voice);
            return;
        case LeaveSetUp::Voice:
            if (streamBusy()) return;
            voiceClipPlay(0x12);
            s.subState = uint8_t(LeaveSetUp::Sequence);
            return;
        case LeaveSetUp::Sequence:
            if (!voiceClipPlaying()) return;
            scriptSequenceStart(stateAddress(TableId::FireRuinDepthsScriptsLeaveCamera), stateAddress(TableId::FireRuinDepthsScriptsLeaveSteps));
            scriptSequenceTick();
            scriptFade(kSkippedFade, 0);
            s.enterStateKeepingPhases(uint8_t(CutsceneState::Steps));
            return;
        default: return;
        }
    case CutsceneState::Steps: runSteps(leaveStep, false); return; // exe 0x52eb60
    case CutsceneState::WrapUp:                                    // exe 0x52f770
        if (!wrapUpReady()) return;
        wrapUpStart();
        spawnRecords(stateAddress(TableId::FireRuinDepthsScriptsLeaveSpawns), 1);
        spawnRecords(stateAddress(TableId::FireRuinDepthsScriptsLeaveRecords), 4);
        spawnRecords(stateAddress(TableId::FireRuinDepthsScriptsLeaveMoreSpawns), 1);
        playerBackAt(0x3000000, 0xffc00000, 0xfb000000, 0);
        soundFadeDip(4, 0x333, 0, 0); // exe 0x5d5830
        cameraState().lookPitch = 0;
        scriptEndBlock();
        return;
    default: notPortedAt(kStoryPort, "script 0x47 state", s.state);
    }
}


} // namespace

bool fireRuinDepthsScriptsPorted(RoutineId handler) {
    return handler == RoutineId::FireRuinEdgeScript || handler == RoutineId::FireRuinReturnScript || handler == RoutineId::FireRuinBeatenScript || handler == RoutineId::FireRuinLeaveScript;
}
bool fireRuinDepthsScriptsTick(const PlayerEnv&, RoutineId handler) {
    if (!fireRuinDepthsScriptsPorted(handler)) return false;
    switch (handler) {
    case RoutineId::FireRuinEdgeScript: edgeScript(); break;
    case RoutineId::FireRuinReturnScript: returnScript(); break;
    case RoutineId::FireRuinBeatenScript: beatenScript(); break;
    case RoutineId::FireRuinLeaveScript: leaveScript(); break;
    default: break;
    }
    return true;
}

} // namespace od::port
