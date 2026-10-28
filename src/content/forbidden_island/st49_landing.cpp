// The landing on the Forbidden Island (see st49_landing.hpp). Script 0x18's states are its exe table's ([0x8a27cc]:
// 0x51ddb0 / 0x51dec0 / 0x51df20); its steps (lists 0x8a2710, and 0x8a2750 on the flag-0x5c1 path) are dispatched by
// the named routine. Step ticks are the scene's timeline.
#include "content/forbidden_island/st49_landing.hpp"
#include "content/forbidden_island/st49_landing_state.hpp"

#include "engine/routine_ids.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/audio_logic.hpp"
#include "game/camera.hpp"
#include "game/cutscene_actor.hpp"
#include "game/display.hpp"
#include "game/draw_commands.hpp"
#include "game/fade_state.hpp"
#include "game/game_state.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/transition.hpp"
#include "game/transition_state.hpp"

using namespace od::port::st49_landing_state;

namespace od::port {

namespace {

constexpr const char* kLandingArea = "island story port"; // the area of its port-gap errors

constexpr uint8_t kSt49 = 0x49;
constexpr uint8_t kScript18 = 0x18;
constexpr uint32_t kFlagFirstLanding = 0x5c0, kFlagReturnLanding = 0x5c1; // either: the scene runs
constexpr uint32_t kFlagLandingSeen = 0x7a0;
// the first landing's sequence and its destination: ST10 sub 0 at (0x800, -0x371, -0x3400) facing a half turn
constexpr uint8_t kSt10 = 0x10;
// the flag-0x5c1 path's sequence and its destination: ST04 sub 0 at (0x108, -1, 0x140) facing yaw 0
constexpr uint8_t kSt04 = 4;
constexpr uint8_t kDestinationFadeIn = 2;
constexpr uint32_t kVoiceFadeStep = 8;
constexpr uint32_t kVoiceDescent = 0x13, kVoiceReturn = 0x14;
// the scene's sounds (flat, forced)
constexpr uint16_t kSoundOpening = 0x2a4, kSoundEngines = 0x2a5, kSoundWind = 0x29f, kSoundHatch = 0x2a0;
constexpr uint32_t kFadeIn = 2, kFadeLand = 3, kFadeReturnIn = 0x10, kFadeOut = 0x13;
// the screen tints the opening step draws every tick (render output)
constexpr uint32_t kOpeningTints[2] = {0x42606060, 0x22686868};
constexpr uint32_t kReturnTint = 0x10101;


ScriptState& block() { return scriptState(); }
int stepTick() { return block().stepTick; }
void sweepActors() { actorsSweep(kSweepActors, kSweepFree); } // exe 0x5e05d0(0xffe, 0x80)
void showPlayer(bool on) { player().setDrawn(on); }
void selectFaces(uint8_t eyes, uint8_t mouth) {
    block().faceTracks.first.select(eyes, 0);
    block().faceTracks.second.select(mouth, 0);
}
void selectEyes(uint8_t seq) { block().faceTracks.first.select(seq, 0); }
void selectMouth(uint8_t seq) { block().faceTracks.second.select(seq, 0); }
void lockSkip(bool on) {
    if (on) block().flags |= kScriptUnskippable;
    else block().flags &= uint8_t(~kScriptUnskippable);
}
// the in-stage warp (no fades) to ST49's sub-room `room`, then the next step
void warpToRoom(uint8_t room, int16_t x, int16_t y, int16_t z) {
    requestTransition(kTransitionWarp, kSt49, room, x, y, z, 0, kNoFade, kNoFade);
    scriptSequenceSkip(1);
}
// the camera shake and the pad's rumble of the descent
void rumble(uint32_t amount) {
    cameraShake(1, amount, 0x80);
    padVibrate(0x601, 0x810c0);
}

// ---- the steps of the first landing (list 0x8a2710) ------------------------------------------------------------------
// 0 exe 0x51e010: at 1 the skip back, the player hidden, a fade in, the opening sound; at 0x78 the warp to sub 0
void stepOpening() {
    const int t = stepTick();
    if (t == 1) {
        lockSkip(false);
        showPlayer(false);
        scriptFade(kFadeIn, 0);
        soundPlayFlatForced(kSoundOpening);
    } else if (t == 0x78) {
        warpToRoom(0, 0, 0, 0);
    }
    for (uint32_t tint : kOpeningTints) dc::screenTint(tint); // exe 0x5f3150: full-screen tints
}
// 1 exe 0x51e0c0: the actors swept, the engines; the voice at 0xb4; once it plays the warp to sub 1
void stepDescentVoice() {
    const int t = stepTick();
    if (t == 0) {
        sweepActors();
        soundPlayFlatForced(kSoundEngines);
        soundPlayFlatForced(kSoundWind);
    } else if (t == 0xb4) {
        voiceClipPlay(kVoiceDescent);
    }
    if (t > 0xb4 && voiceClipPlaying()) warpToRoom(1, 0, 0, 0x10);
}
// 2 exe 0x51e180: the player shown in its stage animations 0 / 1 with its faces; at 300 a shake; at 0x136 the warp to
// sub 2
void stepHatchOpens() {
    const int t = stepTick();
    if (t < 0x3d) {
        if (t == 0x3c) {
            selectMouth(0);
        } else if (t == 0) {
            sweepActors();
            soundPlayFlatForced(kSoundHatch);
            showPlayer(true);
            playerStageAnim(0);
            selectEyes(1);
            selectMouth(0);
        } else if (t == 0x38) {
            playerStageAnim(1);
            selectMouth(1);
        }
    } else if (t == 300) {
        selectEyes(2);
        rumble(0x400);
    } else if (t == 0x136) {
        warpToRoom(2, 0, 0, 0);
    }
}
// 3 exe 0x51e2e0: the player hidden; the next step at 0x73
void stepDropped() {
    const int t = stepTick();
    if (t == 0) showPlayer(false);
    else if (t == 0x73) scriptSequenceSkip(1);
}
// 4 exe 0x51e310: the actors swept; at 0xaf the warp to sub 1
void stepFarBelow() {
    const int t = stepTick();
    if (t == 0) sweepActors();
    else if (t == 0xaf) warpToRoom(1, 0, 0, 0x10);
}
// 5 exe 0x51e390: the player shown in its stage animation 2, shaken every 4 ticks; a fade at 0x78, after it the
// warp to sub 3 at (0x840, -0x370, -0x3400), the shake off (the step tick read unsigned)
void stepRide() {
    const uint32_t t = uint32_t(block().stepTick);
    if (t == 0) {
        sweepActors();
        showPlayer(true);
        playerStageAnim(2);
        selectFaces(2, 2);
    } else if (t == 0x78) {
        scriptFade(kFadeOut, 0);
    }
    if (int32_t(t) > 0x78 && fadeState().idle()) {
        requestTransition(kTransitionWarp, kSt49, 3, 0x840, -0x370, -0x3400, 0, kNoFade, kNoFade);
        cameraShake(1, 0, 0);
        scriptSequenceSkip(1);
        return;
    }
    if ((t & 3) == 0) {
        const uint32_t r = rngStep();
        rumble((r & 0x1ff) + 0x200);
    }
}
// 6 exe 0x51e4c0: the actors swept; at 0x3c the player shown talking (step 9) facing yaw 0x7e0, a fade in; the next
// step at 0x10e (the end)
void stepLanded() {
    const int t = stepTick();
    if (t == 0) {
        sweepActors();
    } else if (t == 0x3c) {
        showPlayer(true);
        player().rotation.y = 0x7e0;
        playerScriptAnim(PlayerAnim::Talk, 9, true);
        selectFaces(0, 0);
        scriptFade(kFadeLand, 0);
    } else if (t == 0x10e) {
        scriptSequenceSkip(1);
    }
}

// ---- the steps of the flag-0x5c1 path (list 0x8a2750) ----------------------------------------------------------------
// 0 exe 0x51e550: a fade in; the voice; once it plays the skip back and the next step
void stepReturnVoice() {
    if (stepTick() == 0) scriptFade(kFadeReturnIn, 0); // exe 0x53f6b0
    ScriptState& s = block();
    if (s.stepPhase == 0) {
        voiceClipPlay(kVoiceReturn); // exe 0x601050
        ++s.stepPhase;
    } else if (s.stepPhase == 1 && voiceClipPlaying()) { // exe 0x601240
        s.stepPhase = 0;
        lockSkip(false);
        scriptSequenceSkip(1); // exe 0x5115c0
    }
    dc::screenTint(kReturnTint); // exe 0x5f3150
}
// 1 exe 0x51e5d0 (a switch on the tick): the player shown in its stage animations 0 / 1, the mouth's keys; at 0xd2 the
// warp to sub 3
void stepReturnHatch() {
    switch (uint32_t(block().stepTick)) {
    case 0:
        sweepActors();
        showPlayer(true);
        playerStageAnim(0);
        selectFaces(1, 0);
        return;
    case 0x5a: playerStageAnim(1); return;
    case 0x78:
    case 0x9b: selectMouth(1); return;
    case 0x91:
    case 0xb9: selectMouth(0); return;
    case 0xd2: warpToRoom(3, 0, 0, 0); return;
    default: return;
    }
}
// 2 exe 0x51e7d0: hidden; a fade at 0x177; from 0x178 once it is done the warp to sub 2
void stepReturnFade() {
    const int t = stepTick();
    if (t == 0) {
        sweepActors();
        showPlayer(false);
    } else if (t == 0x177) {
        scriptFade(kFadeOut, 0);
    }
    if (t >= 0x178 && fadeState().idle()) warpToRoom(2, 0, 0, 0);
}
// 3 exe 0x51e880: the actors swept; a fade in at 0x3c; the next step at 0x10e (the end)
void stepReturnLanded() {
    const int t = stepTick();
    if (t == 0) sweepActors();
    else if (t == 0x3c) scriptFade(kFadeLand, 0);
    else if (t == 0x10e) scriptSequenceSkip(1);
}

void runStep(const RoutineRef& routine) {
    switch (routine.id()) {
    case RoutineId::St49LandingOpeningStep: stepOpening(); return;
    case RoutineId::St49LandingDescentVoiceStep: stepDescentVoice(); return;
    case RoutineId::St49LandingHatchOpensStep: stepHatchOpens(); return;
    case RoutineId::St49LandingDroppedStep: stepDropped(); return;
    case RoutineId::St49LandingFarBelowStep: stepFarBelow(); return;
    case RoutineId::St49LandingRideStep: stepRide(); return;
    case RoutineId::St49LandingLandedStep: stepLanded(); return;
    case RoutineId::St49LandingReturnVoiceStep: stepReturnVoice(); return;
    case RoutineId::St49LandingReturnHatchStep: stepReturnHatch(); return;
    case RoutineId::St49LandingReturnFadeStep: stepReturnFade(); return;
    case RoutineId::St49LandingReturnLandedStep: stepReturnLanded(); return;
    default: notPortedAt(kLandingArea, "script 0x18 step", routine.pointerValue());
    }
}

// ---- the states -------------------------------------------------------------------------------------------------------
// exe 0x51ddb0: the sequence and the destination by the path (the first landing marks kFlagLandingSeen), the replay
// pad; once the player is script driven: idle, its motion cleared, its face tracks, the skip locked, state 1
void setUp() {
    ScriptState& s = block();
    Script18Bytes& bytes = scriptState().part<Script18Bytes>();
    if (s.subState == 0) {
        if (!storyFlag(kFlagReturnLanding)) {
            scriptSequenceStart(stateAddress(TableId::St49LandingFirstCamera), scriptStepList<TableId::St49LandingFirstSteps>);
            bytes.setDestinationWords(kSt10, 0, 0x800, -0x371, -0x3400, 0x800);
            setStoryFlag(kFlagLandingSeen);
        } else {
            scriptSequenceStart(stateAddress(TableId::St49LandingReturnCamera), scriptStepList<TableId::St49LandingReturnSteps>);
            bytes.setDestinationWords(kSt04, 0, 0x108, -1, 0x140, 0);
        }
        scriptReplayPadOn(); // exe 0x5117f0
        ++s.subState;
    } else if (s.subState != 1) {
        return;
    }
    if (playerToScriptIfFree() == 0) {
        playerScriptAnim(PlayerAnim::Idle, 0, true);
        player().clearDirectionOffsetWords();
        s.faceTracks.init(stateAddress(TableId::St49LandingFaceEyesTable), stateAddress(TableId::St49LandingFaceMouthTable));
        selectFaces(0, 0);
        scriptSequenceTick();
        s.enterState(CutsceneState::Steps);
        lockSkip(true);
    }
}
// exe 0x51dec0: the step's routine; the list's end or the skip (on a frame of buffer 1) -> the skip's wipe, state 2;
// the sequence and the player's face tracks
void steps() {
    ScriptState& s = block();
    const ScriptStep step = scriptStepAt(s.steps.value());
    bool toWrapUp = step.id == kScriptStepEnd;
    if (!toWrapUp) {
        runStep(stateRoutine(step.routine));
        toWrapUp = scriptSkipRequested() != 0 && display().drawBuffer != 0;
    }
    if (toWrapUp) {
        scriptFade(kScriptSkipWipe, 0);
        s.enterState(CutsceneState::WrapUp);
    }
    scriptSequenceTick();
    faceTracksTick(s.faceTracks);
}
// exe 0x51df20: idle, the actors swept, the voice faded; once it is idle the request to the destination (fade in 2);
// once taken the player back, shown, the block's end
void wrapUp() {
    ScriptState& s = block();
    const uint8_t st = s.subState;
    if (st == 0) {
        playerScriptAnim(PlayerAnim::Idle, 0, true);
        sweepActors();
        if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFadeStep);
        ++s.subState;
    } else if (st != 1) {
        if (st != 2 || transitionRequest().state != kTransitionNone) return;
        playerBackToPlay();   // exe 0x5723c0
        scriptReplayPadOff(); // exe 0x511890
        showPlayer(true);
        scriptEndBlock();
        return;
    }
    if (!voiceClipPlaying()) { // exe 0x601260
        Script18Bytes& bytes = scriptState().part<Script18Bytes>();
        TransitionRequest& r = transitionRequest();
        r.keepPosition = 0;
        r.state = kTransitionAccepted;
        r.exitId = 0;
        r.exitByte1 = 0;
        r.destStage = bytes.destStage;
        r.destRoom = bytes.destRoom;
        r.destination.x = bytes.destination.x;
        r.destination.y = bytes.destination.y;
        r.destination.z = bytes.destination.z;
        r.destination.yaw = bytes.destination.yaw;
        r.fadeOut = kNoFade;
        r.fadeIn = kDestinationFadeIn;
        ++s.subState;
    }
}
void script18() {
    switch (block().cutsceneState()) {
    case CutsceneState::SetUp: setUp(); return;
    case CutsceneState::Steps: steps(); return;
    case CutsceneState::WrapUp: wrapUp(); return;
    default: notPortedAt(kLandingArea, "script 0x18 state", block().state);
    }
}

// ---- the Flutter (exe 0x49dd70; states [0x8939e0]: bind 0x49ddf0, sink 0x49de60) -----------------------------------
// Hypothesis: the Flutter seen from below as it comes down (type 0x30 is the Flutter in ST39)
enum class FlutterState : uint8_t { Bind = 0, Sink = 1 };
constexpr int16_t kFlutterSmallScale = 0x40; // when +0xc == 0
constexpr int16_t kFlutterSinkSpeed = -0x20;
void flutter(AnimatedActor& a) {
    a.prevAnim = a.anim;
    a.savePreviousPose();
    switch (FlutterState(a.state)) {
    case FlutterState::Bind:
        if (!bindActorModel(a, a.variant)) {
            freeActor(a); // exe 0x5e1490
            break;
        }
        a.rotation.x = 0;
        a.rotation.z = 0;
        a.velocity.clearWords();
        if (a.params[0] == 0) {
            a.scale.x = kFlutterSmallScale;
            a.scale.y = kFlutterSmallScale;
            a.scale.z = kFlutterSmallScale;
        }
        a.anim = 0;
        a.prevAnim = kNoAnimShown;
        a.animKey.frame = 0;
        a.state = uint8_t(FlutterState::Sink);
        a.subState = 0;
        break;
    case FlutterState::Sink:
        if (a.params[0] == 0) integrateVelocity(a, 0, 0, kFlutterSinkSpeed);
        break;
    default: notPortedAt(kLandingArea, "0x49dd70 state", a.state);
    }
    if (!(a.flags & kActorLive)) return;
    animateAsked(a, a.animKey.frame); // exe 0x5e1c30 / 0x5e1e80
}

// ---- the still model (exe 0x5b2960; states [0x8d07b0]: bind 0x5b2980, idle 0x52ef00) ---------------------------------
enum class StillModelState : uint8_t { Bind = 0, Idle = 1 };
void stillModel(StaticActor& a) {
    switch (StillModelState(a.state)) {
    case StillModelState::Bind:
        if (!bindActorModel(a, 0)) {
            freeActor(a);
            return;
        }
        a.flags |= kActorDrawn;
        ++a.state;
        return;
    case StillModelState::Idle: return;
    default: notPortedAt(kStoryPort, "static 0x5b2960 state", a.state);
    }
}

} // namespace

bool st49ScriptPorted(RoutineId handler) { return handler == RoutineId::St49LandingScript; }
bool st49ScriptTick(const PlayerEnv&, RoutineId handler) {
    if (handler != RoutineId::St49LandingScript) return false;
    script18();
    return true;
}

bool st49RoomCaseKnown(uint8_t stage) { return stage == kSt49; }
bool st49RoomCase() {
    if (gameState().stage != kSt49) return false;
    if (storyFlag(kFlagReturnLanding) || storyFlag(kFlagFirstLanding)) scriptRun(kScript18);
    return true;
}

void st49FlutterUpdate(const PlayerEnv&, uint32_t actor) { flutter(animatedActorAt(actor)); }
void st49StillModelUpdate(const PlayerEnv&, uint32_t actor) { stillModel(staticActorAt(actor)); }

} // namespace od::port
