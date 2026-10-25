// The fire mission's result (ST1E, script 0xd, exe handler 0x519df0; see fire_mission.hpp):
//   state 0 (exe 0x519e10): the replay pad, the scripts' solids, the windows closed, the skip locked; the outcome (0
//           success / 1 failure) and its bonus (flag 0x12a: a success within 0xd48 ticks over the three rooms, or a
//           failure that still put out 0x12 fires); the player script driven once it stands free, a pose; after 0x60
//           ticks (a failure: at once) the outcome's camera script and steps
//   state 1 (exe 0x519f60): the step runner (as exe 0x53a5e0 without the face tracks; the skip closes the windows)
//   state 2 (exe 0x519fb0): the wrap up: the player healed, the request to ST3A room 0 (the crash landing)
// Success steps (exe 0x8a0098): 0x51a2b0 (message 0x22, the balance -5000, the in-stage warp to room 2, the
// extinguishers' sounds), 0x51a500 (message 0x1e, the voice, the banner, message 0x1f, the fade)
// Failure steps (exe 0x8a00b0): 0x51a0d0 (the fade out, the actors swept), 0x51a130 (message 0x14, the voice, the
// banner, the poses 0x56 / 0x57, message 0x1f, the fade)
#include "content/intro/mission_result_state.hpp"
#include "content/intro/fire_mission.hpp"

#include "engine/data_source.hpp"
#include "engine/routine_ids.hpp"
#include "game/actor_sweeps.hpp"
#include "game/audio_logic.hpp"
#include "game/display.hpp"
#include "game/fade.hpp"
#include "game/fade_state.hpp"
#include "game/hud.hpp"
#include "game/message.hpp"
#include "game/rewards.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/stage_files.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/transition.hpp"
#include "game/transition_state.hpp"

using namespace od::port::mission_result_state;

namespace od::port {

namespace {

constexpr uint8_t kST1E = 0x1e, kST3A = 0x3a;

// the running script's own bytes as script 0xd's
ResultScriptBytes& resultBytes() { return scriptState().part<ResultScriptBytes>(); }

// the outcome (ScriptState::stepSubPhase): kFailure | kBonus
constexpr uint8_t kSuccess = 0x00, kFailure = 0x01, kBonus = 0x02;
constexpr uint8_t kBonusFires = 0x12;     // a failure's bonus: this many fires put out
constexpr uint32_t kBonusTicks = 0xd48;   // a success's bonus: the three rooms within this many ticks
constexpr int32_t kOutcomeDelay = 0x60;
// the outcome's camera scripts and steps (exe tables 0x8a0090 / 0x8a00c8, by the outcome's failure bit)
// (exe data 0x8a0090 / 0x8a00c8: the camera scripts' and step lists' addresses, as the script state stores them:
// the address is the record's identity)
const DataTable<ScalarTable<uint32_t, 2>> outcomeCameraScripts{TableId::MissionResultOutcomeCameraScripts};
const DataTable<ScalarTable<uint32_t, 2>> outcomeSteps{TableId::MissionResultOutcomeSteps};
// the states (exe table 0x8a00d0; the named routines of engine/routines.csv)
const DataTable<RoutineTable<3>> resultStates{TableId::MissionResultStates};
enum class SetUpSub : uint8_t { Start = 0, TakeOver = 1, Delay = 2 };
enum class WrapUpSub : uint8_t { Stop = 0, Leave = 1, WaitRequest = 2 };

constexpr int8_t kTalkWindow = 0;
constexpr uint32_t kVoiceFade = 8;
constexpr uint32_t kVibrationStop = 0x1000000;
// the actors the result's fades sweep away (all but the player and the group-0 effects)
constexpr uint32_t kSweepResult = kSweepActors & ~kSweepEffectsGroup0;
constexpr uint8_t kResultFadeOut = kFadeOutRequest | 1, kResultFadeIn = kFadeInRequest | 1;
constexpr uint8_t kWarpFadeIn = kFadeInRequest | 2, kTalkFadeOut = kFadeOutRequest | 2;
// the result talk: its messages, its voice and its banner
constexpr int16_t kFailureMessage = 0x14, kSuccessMessage = 0x1e, kClosingMessage = 0x1f, kBalanceMessage = 0x22;
constexpr uint32_t kResultVoice = 0xff01;
constexpr uint16_t kResultBannerX = 0x44, kResultBannerY = 0x30;
constexpr HudBannerStyle kResultBanner = HudBannerStyle::MissionComplete;
constexpr int32_t kTalkDelay = 0x5a, kBannerTicks = 0x77;
// the failure's poses (the item-get animations)
constexpr PlayerAnim kFailurePose = PlayerAnim::ItemGet, kFailurePoseEnd = PlayerAnim::ItemGetHold;
constexpr uint32_t kMusicVoices = 0xf;
constexpr int16_t kMusicFadeStep = 0x333;
// the success's warp (the extinguishers' run) and its sounds
constexpr int kBalanceCost = -5000;
constexpr uint16_t kHoseSound = 0xb8, kSpraySound = 0xb9, kPuffSound = 0x10d, kLastPuffSound = 0x10e;
constexpr uint16_t kDoneSound = 0x153;
constexpr uint8_t kHoseTicks = 0x40, kSprayTicks = 0x30, kPuffTicks = 0x14, kDoneTicks = 0x32;
constexpr int32_t kPuffs = 7;

// the script block
ScriptState& block() { return scriptState(); }
// a step's first phase (every step's phase enum starts there)
constexpr uint8_t kFirstPhase = 0;
// the step moved on, its phase back to its first
void stepDone() {
    scriptSequenceSkip(1);
    block().stepPhase = kFirstPhase;
}

// the fade-out step's phases: the fade asked, waited
enum class FadeOutPhase : uint8_t { Fade = 0, Wait = 1 };

void fadeOutStep() { // exe 0x51a0d0 (the failure's first step)
    ScriptState& s = block();
    if (FadeOutPhase(s.stepPhase) == FadeOutPhase::Fade) {
        scriptFade(kResultFadeOut, 0);
        ++s.stepPhase;
    } else if (FadeOutPhase(s.stepPhase) == FadeOutPhase::Wait && fadeState().idle()) {
        actorsSweep(kSweepResult, kSweepFree);
        scriptFade(kResultFadeIn, 0);
        stepDone();
    }
}

// exe 0x51a130 (the failure) / 0x51a500 (the success): the result talk
enum class TalkPhase : uint8_t { WaitFade = 0, Delay = 1, Voice = 2, Banner = 3, Closing = 4, WaitClose = 5 };

// exe 0x51a130 (failure) / 0x51a500 (success): the result talk
void resultTalkStep(bool failure) {
    ScriptState& s = block();
    auto&& bytes = resultBytes();
    switch (TalkPhase(s.stepPhase)) {
    case TalkPhase::WaitFade:
        if (fadeState().idle()) {
            bytes.ticks = 0;
            ++s.stepPhase;
        }
        return;
    case TalkPhase::Delay:
        bytes.ticks += 1u;
        if (bytes.ticks == kTalkDelay) {
            bytes.ticks = 0;
            windowOpen(kTalkWindow, kMessageBank, failure ? kFailureMessage : kSuccessMessage);
            ++s.stepPhase;
        }
        return;
    case TalkPhase::Voice:
        if (windowStatus(kTalkWindow) == 0 && !(fileLoads().loading & kStreamBusy) && display().drawBuffer == 0 &&
            fadeState().idle()) {
            voiceClipPlay(kResultVoice);
            hudBannerShow(kResultBannerX, kResultBannerY, kResultBanner);
            if (failure) playerScriptAnim(kFailurePose, 0, true);
            bytes.ticks = 0;
            soundFadeOut(kMusicVoices, kMusicFadeStep, 0);
            ++s.stepPhase;
        }
        return;
    case TalkPhase::Banner:
        if (failure) {
            if (bytes.ticks > kBannerTicks && voiceClipIdle()) {
                hudBanner().hide(); // exe 0x5f2680
                s.flags &= uint8_t(~kScriptSkipLocked);
                playerScriptAnim(kFailurePoseEnd, 0, true);
                ++s.stepPhase;
                return;
            }
            bytes.ticks += 1u;
        } else {
            bytes.ticks += 1u;
            if (bytes.ticks > kBannerTicks) {
                hudBanner().hide();
                s.flags &= uint8_t(~kScriptSkipLocked);
                ++s.stepPhase;
            }
        }
        return;
    case TalkPhase::Closing:
        windowOpen(kTalkWindow, kMessageBank, kClosingMessage);
        ++s.stepPhase;
        return;
    case TalkPhase::WaitClose:
        if (windowStatus(kTalkWindow) == 0) {
            scriptFade(kTalkFadeOut, 0);
            stepDone();
        }
        return;
    default: return;
    }
}

enum class WarpPhase : uint8_t { Balance = 0, WaitWindow = 1, Warp = 2, Spray = 3, Puffs = 4, PuffRun = 5, Done = 6,
                                 WaitArrival = 7 };

void successWarpStep() { // exe 0x51a2b0
    ScriptState& s = block();
    auto&& bytes = resultBytes();
    uint8_t& countdown = bytes.countdown;
    switch (WarpPhase(s.stepPhase)) {
    case WarpPhase::Balance:
        windowOpen(kTalkWindow, kMessageBank, kBalanceMessage);
        toolKitBalanceAdd(kBalanceCost);
        ++s.stepPhase;
        return;
    case WarpPhase::WaitWindow:
        if (windowStatus(kTalkWindow) == 0) {
            scriptFade(kResultFadeOut, 0);
            ++s.stepPhase;
        }
        return;
    case WarpPhase::Warp:
        if (fadeState().idle()) {
            requestTransition(kTransitionWarp, kST1E, 2, 0x138, 0, 499, 0x100, kNoFade, kNoFade);
            actorsSweep(kSweepResult, kSweepFree);
            soundPlayFlat(kHoseSound);
            countdown = kHoseTicks;
            ++s.stepPhase;
        }
        return;
    case WarpPhase::Spray:
        countdown -= 1u;
        if (countdown == 0) {
            soundPlayFlat(kSpraySound);
            countdown = kSprayTicks;
            ++s.stepPhase;
        }
        return;
    case WarpPhase::Puffs:
        countdown -= 1u;
        if (countdown == 0) {
            soundPlayFlat(kPuffSound);
            countdown = kPuffTicks;
            bytes.ticks = 0;
            ++s.stepPhase;
        }
        return;
    case WarpPhase::PuffRun:
        countdown -= 1u;
        if (countdown == 0) {
            bytes.ticks += 1u;
            if (bytes.ticks != kPuffs) {
                soundPlayFlat(kPuffSound);
                countdown = kPuffTicks;
                return;
            }
            soundPlayFlat(kLastPuffSound);
            soundPlayFlat(kDoneSound);
            countdown = kDoneTicks;
            ++s.stepPhase;
        }
        return;
    case WarpPhase::Done:
        countdown -= 1u;
        if (countdown == 0) {
            bytes.ticks = 0;
            ++s.stepPhase;
        }
        return;
    case WarpPhase::WaitArrival:
        if (transitionRequest().state == kTransitionNone) {
            scriptFade(kWarpFadeIn, 0);
            stepDone();
        }
        return;
    default: return;
    }
}

void resultSetUp() { // exe 0x519e10
    ScriptState& s = block();
    auto&& bytes = resultBytes();
    switch (SetUpSub(s.subState)) {
    case SetUpSub::Start: {
        scriptReplayPadOn();
        setStoryFlag(kFlagScriptSolids);
        windowCloseAll(WindowClose::Force);
        bytes.ticks = 0;
        s.flags |= kScriptSkipLocked;
        FireMissionState& m = fireMission();
        if (!storyFlag(kMissionSuccessFlag)) {
            s.stepSubPhase = kFailure;
            if (m.firesOut >= kBonusFires) {
                setStoryFlag(kMissionBonusFlag);
                s.stepSubPhase |= kBonus;
            }
        } else {
            s.stepSubPhase = kSuccess;
            if (uint32_t(m.roomTimers[0]) + m.roomTimers[1] + m.roomTimers[2] <= kBonusTicks) {
                setStoryFlag(kMissionBonusFlag);
                s.stepSubPhase |= kBonus;
            }
        }
        s.subState = uint8_t(SetUpSub::TakeOver);
        return;
    }
    case SetUpSub::TakeOver: // exe 0x572320: the player to the script once it stands free
        if (playerToScriptWhenStanding() == 0) {
            playerScriptAnim(PlayerAnim::Idle, 0, true);
            ++s.subState;
        }
        return;
    case SetUpSub::Delay: {
        bytes.ticks += 1u;
        if (bytes.ticks != kOutcomeDelay && !(s.stepSubPhase & kFailure)) return;
        bytes.ticks = 0;
        Player& p = player();
        p.clearDirectionOffsetWords();
        p.idleFaces[0] = 0;
        p.idleFaces[1] = 0;
        p.velocity.clearWords();
        p.acceleration.clearWords();
        const size_t outcome = s.stepSubPhase & kFailure;
        scriptSequenceStart(outcomeCameraScripts[outcome], outcomeSteps[outcome]);
        scriptSequenceTick();
        s.enterStateKeepingPhases(uint8_t(CutsceneState::Steps));
        return;
    }
    default: return;
    }
}

void resultSteps() { // exe 0x519f60
    ScriptState& s = block();
    const ScriptStep step = scriptStepAt(s.steps.value());
    if (step.id != kScriptStepEnd) {
        const RoutineRef routine = stateRoutine(step.routine);
        switch (routine.id()) {
        case RoutineId::MissionResultFadeOutStep: fadeOutStep(); break;
        case RoutineId::MissionResultStep2: resultTalkStep(true); break;
        case RoutineId::MissionResultSuccessWarpStep: successWarpStep(); break;
        case RoutineId::MissionResultStep4: resultTalkStep(false); break;
        default: notPortedAt(kStoryPort, "script 0xd step function", routine.pointerValue());
        }
        if (scriptSkipRequested() == 0 || display().drawBuffer == 0) {
            scriptSequenceTick();
            return;
        }
        windowCloseAll(WindowClose::Force);
        scriptFade(kScriptSkipWipe, 0);
    }
    s.enterState(CutsceneState::WrapUp);
    scriptSequenceTick();
}

void resultWrapUp() { // exe 0x519fb0
    ScriptState& s = block();
    switch (WrapUpSub(s.subState)) {
    case WrapUpSub::Stop:
        padVibrate(0, kVibrationStop);
        playerScriptAnim(PlayerAnim::Idle, 0, true);
        actorsSweep(kSweepActors, kSweepFree);
        windowCloseAll(WindowClose::Force);
        if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade);
        ++s.subState;
        return;
    case WrapUpSub::Leave:
        if (voiceClipIdle() && fadeState().idle()) {
            Player& p = player();
            p.hitPoints = p.maxHitPoints;
            requestTransition(kTransitionAccepted, kST3A, 0, 0, -1, 0, 0, kFadeInRequest | 2, kNoFade);
            ++s.subState;
        }
        return;
    case WrapUpSub::WaitRequest:
        if (transitionRequest().state != kTransitionNone) return;
        playerBackToPlay(); // exe 0x5723c0
        scriptReplayPadOff();
        clearStoryFlag(kFlagScriptSolids);
        player().flags |= kActorDrawn;
        scriptEndBlock();
        return;
    default: return;
    }
}

// exe 0x519df0: the countdown pointer, then the state's routine (exe table 0x8a00d0: the named routines of engine/routines.csv)
void resultScript() {
    ScriptState& s = block();
    resultGlobals().countdown = kScriptBytesAddress;
    switch (resultStates->idAt(s.state)) {
    case RoutineId::MissionResultSetUpState: resultSetUp(); return;
    case RoutineId::MissionResultStepsState: resultSteps(); return;
    case RoutineId::MissionResultWrapUpState: resultWrapUp(); return;
    default: notPortedAt(kStoryPort, "script 0xd state", s.state);
    }
}

} // namespace

bool missionResultScriptPorted(RoutineId handler) { return handler == RoutineId::MissionResultScript; }

bool missionResultScriptTick(const PlayerEnv&, RoutineId handler) {
    if (handler != RoutineId::MissionResultScript) return false;
    resultScript();
    return true;
}

} // namespace od::port
