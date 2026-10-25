// Out of the Abandoned Mine (script 0x12, exe handler 0x51b820; started by the room handler 0x58aab0; see abandoned_mine_exit.hpp):
//   state 0 (exe 0x51b840): the camera script and the steps, the replay pad, the player placed at (-0x10, 0, 0x40) yaw
//           0xab8, its motion cleared, its face tracks once script driven
//   state 1: the generic step runner (game/script_engine); the step 0x51b9e0: the messages 0x64, 0x67, 0x69, 0x72, 0x73
//           in window 0, then 0x32 ticks and state 2
//   state 2 (exe 0x51b910): the fade, the windows closed, the script actor of the record 0x8a0678 freed, the player
//           back
#include "content/yosyonke/abandoned_mine_exit.hpp"
#include "content/yosyonke/abandoned_mine_exit_state.hpp"

#include "engine/routine_ids.hpp"
#include "game/audio_logic.hpp"
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
#include "game/story.hpp"
#include "game/transition.hpp"
#include "game/transition_state.hpp"

using namespace od::port::abandoned_mine_exit_state;

namespace od::port {

namespace {

constexpr uint32_t kVoiceFade = 8;
constexpr uint32_t kVibrationStop = 0x1000000;
constexpr uint8_t kExitFadeOut = kFadeOutRequest | 2, kExitFadeIn = kFadeInRequest | 1;
constexpr int8_t kTalkWindow = 0;
// the player's place
constexpr int16_t kPlayerX = -0x10, kPlayerY = 0, kPlayerZ = 0x40, kPlayerYaw = 0xab8;
// the talk's messages: the first, then the four that follow
constexpr int16_t kFirstMessage = 100;
constexpr int16_t kNextMessages[] = {0x67, 0x69, 0x72, 0x73};
constexpr uint8_t kLastMessageSub = 4, kWaitLastSub = 5, kCountdownSub = 6;
constexpr uint8_t kCountdownTicks = 0x32;

// the running script's own bytes as script 0x12's
RuinExitBytes& exitBytes() { return scriptState().part<RuinExitBytes>(); }

// the script block
ScriptState& block() { return scriptState(); }

void ruinExitWrapUp() { // exe 0x51b910
    ScriptState& s = block();
    switch (s.subState) {
    case 0:
        padVibrate(0, kVibrationStop);
        playerScriptAnim(PlayerAnim::Idle, 0, true);
        if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade);
        ++s.subState;
        if (!(s.flags & kScriptSkipTaken)) scriptFade(kExitFadeOut, 0);
        [[fallthrough]];
    case 1:
    case 2:
    case 3:
        if (fadeState().idle()) {
            s.subState = 4;
            windowCloseAll(WindowClose::Force);
            scriptActorFreeRecord(stateAddress(TableId::AbandonedMineExitGuideRecord));
            if (fadeState().covered != 0) scriptFade(kExitFadeIn, 0);
        }
        return;
    case 4:
        if (transitionRequest().state != kTransitionNone) return;
        playerBackToPlay(); // exe 0x5723c0
        scriptReplayPadOff();
        player().setDrawn(true);
        scriptEndBlock();
        return;
    default: return;
    }
}

void talkStep() { // exe 0x51b9e0
    ScriptState& s = block();
    const uint8_t st = s.subState;
    if (st == 0) {
        windowOpen(kTalkWindow, kMessageBank, kFirstMessage);
        s.subState = 1;
    }
    if (st <= kLastMessageSub) {
        if (windowStatus(kTalkWindow) != 0) return;
        const uint8_t k = st == 0 ? 0 : uint8_t(st - 1u);
        s.subState = uint8_t(k + 2u);
        s.stepPhase = 0;
        s.stepSubPhase = 0;
        if (k < 4) windowOpen(kTalkWindow, kMessageBank, kNextMessages[k]);
        return;
    }
    if (st == kWaitLastSub) {
        if (windowStatus(kTalkWindow) != 0) return;
        s.subState = kCountdownSub;
        s.stepPhase = 0;
        s.stepSubPhase = 0;
        exitBytes().setCountdownBytes(kCountdownTicks, 0);
        return;
    }
    if (st != kCountdownSub) return;
    auto&& b = exitBytes();
    b.countdown = int16_t(b.countdown - 1);
    if (b.countdown != 0) return;
    s.enterState(CutsceneState::WrapUp);
    ruinExitWrapUp();
}

// the steps' routines (by the named routine)
void ruinExitStep(const RoutineRef& routine) {
    switch (routine.id()) {
    case RoutineId::AbandonedMineExitTalkStep: talkStep(); return;
    default: notPortedAt(kStoryPort, "script step function", routine.pointerValue());
    }
}

void ruinExitSetUp() { // exe 0x51b840
    ScriptState& s = block();
    if (s.subState == 0) {
        scriptSequenceStart(stateAddress(TableId::AbandonedMineExitCameraScript), stateAddress(TableId::AbandonedMineExitSteps));
        scriptReplayPadOn();
        Player& p = player();
        p.pos.x.whole = kPlayerX;
        p.pos.y.whole = kPlayerY;
        p.pos.z.whole = kPlayerZ;
        p.rotation.y = kPlayerYaw;
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
        s.faceTracks.init(stateAddress(TableId::AbandonedMineExitFaceEyes), stateAddress(TableId::AbandonedMineExitFaceMouth));
        s.faceTracks.first.select(0, 0);
        s.faceTracks.second.select(0, 0);
        s.enterState(CutsceneState::Steps);
    }
    scriptSequenceTick();
}

// exe 0x51b820: the state
void ruinExitScript() {
    ScriptState& s = block();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: ruinExitSetUp(); return;
    case CutsceneState::Steps: scriptStepsState(&ruinExitStep); return;
    case CutsceneState::WrapUp: ruinExitWrapUp(); return;
    default: notPortedAt(kStoryPort, "script 0x12 state", s.state);
    }
}

} // namespace

bool abandonedMineExitScriptPorted(RoutineId handler) { return handler == RoutineId::AbandonedMineExitScript; }

bool abandonedMineExitScriptTick(const PlayerEnv&, RoutineId handler) {
    if (handler != RoutineId::AbandonedMineExitScript) return false;
    ruinExitScript();
    return true;
}

} // namespace od::port
