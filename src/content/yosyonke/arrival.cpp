// The arrival on Kattelox after the crash landing (see arrival.hpp):
// script 0x55 (exe 0x532ec0; ST08): state 0 (exe 0x532ee0) the replay pad, the player hidden, the room's first actor
//   (the Flutter) lifted 0x400 and pushed 0x80 back to come down onto its place, voice clip 0xf, then the camera script
//   and its single step and a fade in; state 1 (exe 0x532fd0) the descent (the speed eases, it lands and slides to its
//   place, 0x1e ticks, the fade out; the skip); state 2 (exe 0x533130) the Flutter freed, the actors swept, the request
//   to ST04 room 0; flag 0x780, the block's end.
// script 0x4c (exe 0x5302d0; ST04): state 0 (exe 0x5302f0) the camera script and the steps, the replay pad, the
//   player's motion cleared, its face tracks once script driven; state 1 the generic step runner (game/script_engine) with the
//   steps 0x5304c0 (message 0xc9), 0x530510 (after 0x14 ticks message 0xca), 0x530580 (message 0xcb, then state 2);
//   state 2 (exe 0x530390) the fade, the windows closed, the actors swept, the request to ST08 room 0.
#include "content/yosyonke/arrival.hpp"
#include "content/yosyonke/arrival_state.hpp"

#include "engine/routine_ids.hpp"
#include "game/room_scripts.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_sweeps.hpp"
#include "game/audio_logic.hpp"
#include "game/display.hpp"
#include "game/fade.hpp"
#include "game/fade_state.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/message.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/spawn.hpp"
#include "game/stage_files.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/transition.hpp"
#include "game/transition_state.hpp"

using namespace od::port::arrival_state;

namespace od::port {

namespace {

constexpr uint8_t kST04 = 4, kST08 = 8;
constexpr uint32_t kVoiceFade = 8;
constexpr uint32_t kVibrationStop = 0x1000000;
constexpr uint8_t kLandedFadeOut = kFadeOutRequest | 2;

// the script block
ScriptState& block() { return scriptState(); }

// ---- script 0x55: the Flutter comes down
// ------------------------------------------------------------------------------
// the running script's own bytes as script 0x55's
DescentScriptBytes& descentBytes() { return scriptState().part<DescentScriptBytes>(); }
constexpr size_t kFlutterActor = 0; // the script's actor (and the room's first): the Flutter
constexpr uint32_t kDescentVoice = 0xf;
constexpr uint32_t kDescentDoneFlag = 0x780;
constexpr int16_t kLift = 0x400, kPushBack = 0x80, kFallSpeed = 0x100, kFallBrake = -0x10, kSlowFall = 0x20;
constexpr int16_t kSlideSpeed = -0x20, kLandingNear = 0x200, kLandingClose = 0x100;
constexpr int32_t kLandedTicks = 0x1e;
// the Flutter's bytes the script sets (its animation 1 at key 0; its engine's 0x40 at the landing, once)
constexpr uint8_t kFlutterAnim = 1, kEngineDown = 0x40, kEngineDownSet = 1;
enum class DescentSetUpSub : uint8_t { Lift = 0, WaitFile = 1, WaitStream = 2, WaitVoice = 3 };
enum class DescentFallSub : uint8_t { Fall = 0, Brake = 1, Land = 2, Slide = 3, Landed = 4, FadeOut = 5 };

void descentSetUp() { // exe 0x532ee0
    ScriptState& s = block();
    switch (DescentSetUpSub(s.subState)) {
    case DescentSetUpSub::Lift: {
        scriptReplayPadOn();
        const uint32_t flutterAt = roomScriptState().actors[0];
        player().setDrawn(false);
        pointAtWord(s.actors[kFlutterActor], flutterAt);
        auto&& b = descentBytes();
        Actor& a = actorLayoutAt(flutterAt);
        b.landingY = a.pos.y.whole;
        a.pos.y.whole -= uint16_t(kLift);
        a.velocity.y = kFallSpeed;
        a.acceleration.y = kFallBrake;
        b.landingZ = a.pos.z.whole;
        a.pos.z.whole += uint16_t(kPushBack);
        a.velocity.z = 0;
        a.acceleration.z = 0;
        a.params[0] = kFlutterAnim;
        a.params[1] = 0;
        ++s.subState;
        [[fallthrough]];
    }
    case DescentSetUpSub::WaitFile:
        if (!(fileLoads().loading & kFileLoading)) ++s.subState;
        return;
    case DescentSetUpSub::WaitStream:
        if (!(fileLoads().loading & kStreamBusy)) {
            voiceClipPlay(kDescentVoice);
            ++s.subState;
        }
        return;
    case DescentSetUpSub::WaitVoice:
        if (voiceClipPlaying()) {
            scriptSequenceStart(stateAddress(TableId::ArrivalDescentCameraScript), scriptStepList<TableId::ArrivalDescentSteps>);
            scriptSequenceTick();
            scriptFade(kFadeInRequest | 2, 0);
            s.enterState(CutsceneState::Steps);
        }
        return;
    default: return;
    }
}

void descentFall() { // exe 0x532fd0
    ScriptState& s = block();
    auto&& b = descentBytes();
    Actor& a = actorLayoutOf(*s.actors[kFlutterActor]);
    switch (DescentFallSub(s.subState)) {
    case DescentFallSub::Fall:
        if (a.step == 0 && int(b.landingY) - kLandingNear <= int(a.pos.y.whole)) {
            a.params[1] = kEngineDown;
            a.step = kEngineDownSet;
        }
        if (int(b.landingY) - kLandingClose <= int(a.pos.y.whole)) ++s.subState;
        break;
    case DescentFallSub::Brake:
        a.velocity.y = int16_t(a.velocity.y + a.acceleration.y);
        if (a.velocity.y < kSlowFall + 1) {
            a.velocity.y = kSlowFall;
            a.acceleration.y = 0;
            ++s.subState;
        }
        break;
    case DescentFallSub::Land:
        if (b.landingY <= a.pos.y.whole) {
            a.pos.y.whole = b.landingY;
            a.velocity.y = 0;
            a.acceleration.y = 0;
            a.velocity.z = kSlideSpeed;
            ++s.subState;
        }
        break;
    case DescentFallSub::Slide:
        if (a.pos.z.whole <= b.landingZ) {
            a.pos.z.whole = b.landingZ;
            a.velocity.z = 0;
            b.ticks = 0;
            ++s.subState;
        }
        break;
    case DescentFallSub::Landed:
        if (b.ticks == kLandedTicks) {
            scriptFade(kLandedFadeOut, 0);
            s.flags |= kScriptUnskippable;
            ++s.subState;
        } else {
            b.ticks += 1u;
        }
        break;
    case DescentFallSub::FadeOut:
        if (fadeState().idle()) s.enterState(CutsceneState::WrapUp);
        break;
    default: break;
    }
    integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
    if (scriptSkipRequested() != 0 && display().drawBuffer != 0) {
        scriptFade(kScriptSkipWipe, 0);
        s.enterState(CutsceneState::WrapUp);
    }
    scriptSequenceTick();
}

void descentWrapUp() { // exe 0x533130
    ScriptState& s = block();
    const uint8_t c = s.subState;
    if (c == 0) {
        if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade);
        ++s.subState;
        return;
    }
    if (c == 1) {
        if (voiceClipIdle() && fadeState().idle()) {
            freeActor(*s.actors[kFlutterActor]); // exe 0x5e1490
            actorsSweep(kSweepTransition, kSweepFree);
            requestTransition(kTransitionAccepted, kST04, 0, 0, -1, 0, 0, kFadeInRequest | 2, kNoFade);
            ++s.subState;
        }
        return;
    }
    if (c != 2 || transitionRequest().state != kTransitionNone) return;
    setStoryFlag(kDescentDoneFlag);
    scriptReplayPadOff();
    player().setDrawn(true);
    scriptEndBlock();
}

// exe 0x532ec0: the state
void descentScript() {
    ScriptState& s = block();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: descentSetUp(); return;
    case CutsceneState::Steps: descentFall(); return;
    case CutsceneState::WrapUp: descentWrapUp(); return;
    default: notPortedAt(kStoryPort, "script 0x55 state", s.state);
    }
}

// ---- script 0x4c: the three messages in the cabin
// ---------------------------------------------------------------------
// the running script's own bytes as script 0x4c's
CabinScriptBytes& cabinBytes() { return scriptState().part<CabinScriptBytes>(); }
constexpr int8_t kCabinWindow = 1;
constexpr int16_t kFirstMessage = 0xc9, kSecondMessage = 0xca, kLastMessage = 0xcb;
constexpr uint8_t kSecondMessageDelay = 0x14;
constexpr int32_t kSecondMessageTicks = 0x4f;
// the actors the cabin's end sweeps away (the groups 0 / 2 of the animated pool, the statics' group 2, the projectiles,
// the effects' group 2)
constexpr uint32_t kSweepCabin = kSweepAnimatedGroup0 | kSweepAnimatedGroup2 | kSweepProjectiles | kSweepStaticsGroup2 |
                                 kSweepEffectsGroup2;
enum class MessageSub : uint8_t { Open = 0, WaitClose = 1, Second = 2 };

void cabinWrapUp() { // exe 0x530390
    ScriptState& s = block();
    switch (s.subState) {
    case 0:
        padVibrate(0, kVibrationStop);
        playerScriptAnim(PlayerAnim::Idle, 0, true);
        if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade);
        ++s.subState;
        if (!(s.flags & kScriptSkipTaken)) scriptFade(kLandedFadeOut, 0);
        [[fallthrough]];
    case 1:
    case 2:
    case 3:
        if (fadeState().idle()) {
            windowCloseAll(WindowClose::Force);
            actorsSweep(kSweepCabin, kSweepFree);
            s.subState = 4;
            requestTransition(kTransitionAccepted, kST08, 0, 0x40, 0, 0, 0x800, kFadeInRequest | 3, kNoFade);
        }
        return;
    case 4:
        if (transitionRequest().state != kTransitionNone) return;
        playerBackToPlay(); // exe 0x5723c0
        scriptReplayPadOff();
        player().specialAllowed = 0;
        player().setDrawn(true);
        scriptEndBlock();
        return;
    default: return;
    }
}

// exe 0x5304c0 (message 0xc9) / 0x530580 (message 0xcb, then the wrap up at once): window 1 on the message, the next
// step once it is closed
void messageStep(int16_t message, bool last) {
    ScriptState& s = block();
    if (s.subState == uint8_t(MessageSub::Open)) {
        windowOpen(kCabinWindow, kMessageBank, message);
        s.subState = uint8_t(MessageSub::WaitClose);
    } else if (s.subState != uint8_t(MessageSub::WaitClose)) {
        return;
    }
    if (windowStatus(kCabinWindow) != 0) return;
    if (last) {
        s.enterState(CutsceneState::WrapUp);
        cabinWrapUp();
        return;
    }
    s.subState = uint8_t(MessageSub::Open);
    s.clearStepPhase();
    scriptSequenceSkip(1);
}

void delayedMessageStep() { // exe 0x530510
    ScriptState& s = block();
    auto&& b = cabinBytes();
    const uint8_t c = s.subState;
    if (c == 0) {
        b.delay = kSecondMessageDelay;
        s.subState = 1;
    } else if (c == 1) {
        b.delay -= 1u;
        if (b.delay == 0) {
            windowOpen(kCabinWindow, kMessageBank, kSecondMessage);
            s.subState = 2;
        }
    } else if (c == 2 && s.stepTick > kSecondMessageTicks && windowStatus(kCabinWindow) == 0) {
        s.subState = 0;
        s.clearStepPhase();
        scriptSequenceSkip(1);
    }
}

// the steps' routines (by the named routine)
void cabinStep(const RoutineRef& routine) {
    switch (routine.id()) {
    case RoutineId::ArrivalCabinStep1: messageStep(kFirstMessage, false); return;
    case RoutineId::ArrivalCabinDelayedMessageStep: delayedMessageStep(); return;
    case RoutineId::ArrivalCabinStep3: messageStep(kLastMessage, true); return;
    default: notPortedAt(kStoryPort, "script 0x4c step function", routine.pointerValue());
    }
}

void cabinSetUp() { // exe 0x5302f0
    ScriptState& s = block();
    if (s.subState == 0) {
        scriptSequenceStart(stateAddress(TableId::ArrivalCabinCameraScript), scriptStepList<TableId::ArrivalCabinSteps>);
        scriptReplayPadOn();
        Player& p = player();
        p.setPrevMoveDir(MoveDir::Forward);
        p.setMoveDir(MoveDir::Forward);
        p.clearDirectionOffsetWords();
        s.clearScriptByteRun();
        s.subState = 1;
    } else if (s.subState != 1) {
        scriptSequenceTick();
        return;
    }
    if (playerToScriptIfFree() == 0) {
        s.faceTracks.init(stateAddress(TableId::ArrivalCabinEyes), stateAddress(TableId::ArrivalCabinMouth));
        s.faceTracks.first.select(0, 0);
        s.faceTracks.second.select(0, 0);
        s.enterState(CutsceneState::Steps);
    }
    scriptSequenceTick();
}

// exe 0x5302d0: the state
void cabinScript() {
    ScriptState& s = block();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: cabinSetUp(); return;
    case CutsceneState::Steps: scriptStepsState(&cabinStep); return;
    case CutsceneState::WrapUp: cabinWrapUp(); return;
    default: notPortedAt(kStoryPort, "script 0x4c state", s.state);
    }
}

} // namespace

bool arrivalScriptPorted(RoutineId handler) {
    return handler == RoutineId::ArrivalDescentScript || handler == RoutineId::ArrivalCabinScript;
}

bool arrivalScriptTick(const PlayerEnv&, RoutineId handler) {
    if (handler == RoutineId::ArrivalDescentScript) {
        descentScript();
        return true;
    }
    if (handler == RoutineId::ArrivalCabinScript) {
        cabinScript();
        return true;
    }
    return false;
}

} // namespace od::port
