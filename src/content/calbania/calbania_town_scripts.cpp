// The Calbania town's scripts (ST20 sub 0):
//   0x10 (exe 0x51b090, states 0x8a0490: 0x51b0b0 / 0x51b180 / 0x51b1c0; camera 0x8a03a8, steps 0x8a0428): story 8,
//   the first visit of the town (room handler 0x5813b0, flag 0x590). State 0: the sequence, the replay pad, the player
//   at (0x5c0, -0x280, 0xe0) yaw 0x400; once script driven the face tracks 0x8a0480 / 0x8a0488. Steps: 0x51b290
//   (animation 0; at 0x3c message 0xf in window 1), 0x51b2e0 (message 0x10 in window 0, animation 0; next once window 0
//   is closed), 0x51b340 (animation 0; next at 0x1e). State 2: vibration off, fade 0x12 (unless skipped), the windows
//   closed, the actors of classes 0x496 swept, fade 2, the player back at the same place.
//   0x11 (exe 0x51b390, states 0x8a0668: 0x51b3b0 / 0x531310 / 0x51b470; camera 0x8a04f0, steps 0x8a05f0, face tracks
//   0x8a0658 / 0x8a0660): story 9, back in the town after Glyde's base (room handler 0x581440, flag 0x590). State 0:
//   the sequence, the replay pad, the player at (-0x4e0, -0x280, 0x6a0) yaw 0x280; once script driven the face tracks.
//   Steps (window 0 of bank 0x73c000, next once it is closed): 0x51b560 (animation 0, message 0x32; the upper body
//   turned toward 0x190 by 0x20; face keys at 0xa / 0x64), 0x51b650 (0x33), 0x51b6a0 (0x34; toward 0x480 by 0x40; face
//   keys at 0x1e / 0x64), 0x51b780 (0x35), 0x51b7d0 (0x36 -> state 2 at once). The runner 0x531310 (as
//   content/yosyonke/town_story's). State 2: vibration off, fade 0x12 (unless skipped), the windows closed, the actors 0x496
//   swept (fade 2 if covered), the player back at (-0x51c, -0x280, 0x68e) yaw 0, flag 0x443, the block ended.
#include "content/calbania/calbania_town_scripts_state.hpp"
#include "content/calbania/ch3_scripts.hpp"

#include "game/actor_sweeps.hpp"
#include "game/audio_logic.hpp"
#include "game/display.hpp"
#include "game/fade_state.hpp"
#include "game/message.hpp"
#include "game/player/player_input.hpp"
#include "game/port_gap.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/transition.hpp"

using namespace od::port::calbania_town_scripts_state;

namespace od::port {

namespace {

enum class SetUpSub : uint8_t { Start = 0, TakeOver = 1 };
enum class WrapUpSub : uint8_t { Stop = 0, Wait = 1, Close = 2, End = 3 };
enum class LineStep : uint8_t { Open = 0, Wait = 1 };
constexpr uint32_t kMessageBank = 0x73c000;
constexpr uint32_t kVibrationStop = 0x1000000;
constexpr uint32_t kFadeOut = 0x12, kFadeIn = 2, kSkipFade = 0x22;
constexpr uint32_t kSweepMask = 0x496, kSweepKeep = 0x80;

// the shared wrap-up of both scripts: vibration off, fade 0x12 (unless skipped), the windows closed, the actors swept
// and the fade in; true once the player can be put back
bool townWrapUp(ScriptState& s, bool fadeInIfCovered) {
    switch (WrapUpSub(s.subState)) {
    case WrapUpSub::Stop:
        padVibrate(0, kVibrationStop); // exe 0x55cd10
        if (!(s.flags & kScriptSkipTaken)) scriptFade(kFadeOut, 0);
        ++s.subState;
        [[fallthrough]];
    case WrapUpSub::Wait: s.subState = uint8_t(WrapUpSub::Close); return false;
    case WrapUpSub::Close:
        if (fadeState().busy != 0) return false;
        windowCloseAll(WindowClose::Force);  // exe 0x5577d0
        actorsSweep(kSweepMask, kSweepKeep); // exe 0x5e05d0
        s.subState = uint8_t(WrapUpSub::End);
        if (!fadeInIfCovered || fadeState().covered != 0) scriptFade(kFadeIn, 0);
        return false;
    case WrapUpSub::End: return true;
    default: return false;
    }
}

// ==== script 0x10 ========================================================================================================
constexpr RoutineId kFirstVisitStepHello = RoutineId::CalbaniaTownScriptsFirstVisitStepHello,
                    kFirstVisitStepTalk = RoutineId::CalbaniaTownScriptsFirstVisitStepTalk,
                    kFirstVisitStepWait = RoutineId::CalbaniaTownScriptsFirstVisitStepWait;
constexpr int16_t kFirstVisitX = 0x5c0, kFirstVisitY = int16_t(0xfd80), kFirstVisitZ = 0xe0, kFirstVisitYaw = 0x400;

void firstVisitStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    const int32_t t = s.stepTick;
    switch (routine.id()) {
    case kFirstVisitStepHello:
        if (LineStep(s.subState) == LineStep::Open) {
            playerAnim(0); // exe 0x5723d0
            s.subState = uint8_t(LineStep::Wait);
        } else if (LineStep(s.subState) != LineStep::Wait) {
            return;
        }
        if (t == 0x3c) {
            windowOpen(1, kMessageBank, 0xf);
            nextStepFromSub(); // exe 0x5115c0(1)
        }
        return;
    case kFirstVisitStepTalk:
        if (LineStep(s.subState) == LineStep::Open) {
            windowOpen(0, kMessageBank, 0x10);
            playerAnim(0);
            s.subState = uint8_t(LineStep::Wait);
        } else if (LineStep(s.subState) != LineStep::Wait) {
            return;
        }
        if (windowStatus(0) == 0) nextStepFromSub();
        return;
    case kFirstVisitStepWait:
        if (LineStep(s.subState) == LineStep::Open) {
            playerAnim(0);
            s.subState = uint8_t(LineStep::Wait);
        } else if (LineStep(s.subState) != LineStep::Wait) {
            return;
        }
        if (t == 0x1e) nextStepFromSub();
        return;
    default: notPortedAt(kStoryPort, "script 0x10 step", routine.pointerValue());
    }
}

} // namespace

void townFirstVisitScript() {
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x51b0b0
        if (SetUpSub(s.subState) == SetUpSub::Start) {
            scriptSequenceStart(stateAddress(TableId::CalbaniaTownScriptsFirstVisitCamera), stateAddress(TableId::CalbaniaTownScriptsFirstVisitSteps));
            scriptReplayPadOn(); // exe 0x5117f0
            clearPlayerDirections();
            placePlayerWhole(kFirstVisitX, kFirstVisitY, kFirstVisitZ, kFirstVisitYaw);
            s.clearScriptByteRun();
            s.subState = uint8_t(SetUpSub::TakeOver);
        } else if (SetUpSub(s.subState) != SetUpSub::TakeOver) {
            scriptSequenceTick();
            return;
        }
        if (playerToScriptIfFree() == 0) { // exe 0x572300
            startFaceTracks(stateAddress(TableId::CalbaniaTownScriptsFirstVisitEyes), stateAddress(TableId::CalbaniaTownScriptsFirstVisitMouth)); // exe 0x5e33a0
            s.enterState(CutsceneState::Steps);
        }
        scriptSequenceTick();
        return;
    case CutsceneState::Steps: { // exe 0x51b180: the steps (no skip)
        if (s.steps->id == kScriptStepEnd) {
            s.enterState(CutsceneState::WrapUp);
        } else if (const RoutineRef routine = stateRoutine(s.steps->routine)) {
            firstVisitStep(routine);
        }
        scriptSequenceTick();
        faceTracksTick(s.faceTracks);
        return;
    }
    case CutsceneState::WrapUp: // exe 0x51b1c0
        if (!townWrapUp(s, false)) return;
        playerBackToPlay();   // exe 0x5723c0
        scriptReplayPadOff(); // exe 0x511890
        player().pos.x.whole = kFirstVisitX;
        player().setDrawn(true);
        player().pos.y.whole = kFirstVisitY;
        player().pos.z.whole = kFirstVisitZ;
        player().rotation.y = kFirstVisitYaw;
        scriptEndBlock();
        return;
    default: notPortedAt(kStoryPort, "script 0x10 state", s.state);
    }
}

namespace {

// ==== script 0x11 ========================================================================================================
enum class LookPhase : uint8_t { Set = 0, Done = 1 };
enum class FaceStep : uint8_t { Start = 0, Key = 1, Last = 2 };
constexpr RoutineId kStepFirstLine = RoutineId::CalbaniaTownScriptsStepFirstLine,
                    kStepSecondLine = RoutineId::CalbaniaTownScriptsStepSecondLine,
                    kStepThirdLine = RoutineId::CalbaniaTownScriptsStepThirdLine,
                   kStepFourthLine = RoutineId::CalbaniaTownScriptsStepFourthLine,
                   kStepLastLine = RoutineId::CalbaniaTownScriptsStepLastLine;
constexpr int16_t kUpperBodyLimit = 0x300;
constexpr uint32_t kFlagTownReturned = 0x443;

// the shared head of the line steps: window 0 on the message, then wait
void openLine(ScriptState& s, int16_t msg) {
    if (LineStep(s.subState) == LineStep::Open) {
        windowOpen(0, kMessageBank, msg); // exe 0x557390
        s.subState = uint8_t(LineStep::Wait);
    }
}
// the face keys of the first and the third line (on the second phase byte)
void lineFaceKeys(ScriptState& s, int32_t keyTick) {
    const int32_t t = s.stepTick;
    switch (FaceStep(s.stepSubPhase)) {
    case FaceStep::Start:
        faceKeys(0);
        s.stepSubPhase = uint8_t(FaceStep::Key);
        [[fallthrough]];
    case FaceStep::Key:
        if (t == keyTick) {
            eyesKey(1);
            s.stepSubPhase = uint8_t(FaceStep::Last);
        }
        return;
    case FaceStep::Last:
        if (t == 0x64) faceKeys(0);
        return;
    default: return;
    }
}
void townReturnWrapUp(ScriptState& s);
void townReturnStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    switch (routine.id()) {
    case kStepFirstLine:
    case kStepThirdLine: {
        const bool first = routine.is(kStepFirstLine);
        if (LineStep(s.subState) == LineStep::Open) {
            if (first) playerAnim(0);
            openLine(s, first ? 0x32 : 0x34);
        }
        if (LineStep(s.subState) == LineStep::Wait) {
            if (LookPhase(s.stepPhase) == LookPhase::Set) {
                TownReturnBytes& look = scriptState().part<TownReturnBytes>();
                look.lookYaw = int16_t(first ? 0x190 : 0x480);
                look.lookStep = int16_t(first ? 0x20 : 0x40);
                s.stepPhase = uint8_t(LookPhase::Done);
            }
            if (windowStatus(0) == 0) nextStepFromSub(); // exe 0x557810
        }
        lineFaceKeys(s, first ? 0xa : 0x1e);
        return;
    }
    case kStepSecondLine:
    case kStepFourthLine:
        openLine(s, routine.is(kStepSecondLine) ? 0x33 : 0x35);
        if (LineStep(s.subState) == LineStep::Wait && windowStatus(0) == 0) nextStepFromSub();
        return;
    case kStepLastLine:
        openLine(s, 0x36);
        if (LineStep(s.subState) == LineStep::Wait && windowStatus(0) == 0) {
            s.enterState(CutsceneState::WrapUp);
            townReturnWrapUp(s);
        }
        return;
    default: notPortedAt(kStoryPort, "script 0x11 step", routine.pointerValue());
    }
}
// exe 0x531310 (as content/yosyonke/town_story's runner, with this script's steps): the step, the skip, the player's upper
// body turned toward the look yaw by its step
void townReturnSteps(ScriptState& s) {
    bool wrapUp = s.steps->id == kScriptStepEnd;
    if (!wrapUp) {
        const RoutineRef routine = stateRoutine(s.steps->routine);
        if (routine) townReturnStep(routine);
        if (scriptSkipRequested() != 0 && display().drawBuffer != 0) {
            scriptFade(kSkipFade, 0);
            wrapUp = true;
        }
    }
    if (wrapUp) s.enterState(CutsceneState::WrapUp);
    Player& p = player();
    TownReturnBytes& look = scriptState().part<TownReturnBytes>();
    const uint16_t h = uint16_t(p.leanTarget);
    int d = int((uint32_t(h) + uint16_t(p.rotation.y)) & 0xfff) - int(uint16_t(look.lookYaw) & 0xfff);
    if (uint32_t(d + 0x800) > 0x1000u) d = int((uint32_t(-d) ^ 0xfffu) + 1u);
    const int16_t step = look.lookStep;
    const int half = int(step) >> 1;
    if (d < -half) {
        p.leanTarget = int16_t(uint16_t(h + step));
        if (int16_t(h + step) > kUpperBodyLimit) p.leanTarget = kUpperBodyLimit;
    } else if (d > half) {
        p.leanTarget = int16_t(uint16_t(h - step));
        if (int16_t(h - step) < -kUpperBodyLimit) p.leanTarget = int16_t(-kUpperBodyLimit);
    }
    scriptSequenceTick();
    faceTracksTick(s.faceTracks);
}
// exe 0x51b470
void townReturnWrapUp(ScriptState& s) {
    if (!townWrapUp(s, true)) return;
    playerBackToPlay();   // exe 0x5723c0
    scriptReplayPadOff(); // exe 0x511890
    Player& p = player();
    p.pos.x.whole = int16_t(0xfae4);
    p.setDrawn(true);
    p.pos.y.whole = int16_t(0xfd80);
    p.pos.z.whole = 0x68e;
    p.rotation.y = 0;
    setStoryFlag(kFlagTownReturned);
    scriptEndBlock();
}

} // namespace

void townReturnScript() {
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x51b3b0
        if (SetUpSub(s.subState) == SetUpSub::Start) {
            scriptSequenceStart(stateAddress(TableId::CalbaniaTownScriptsTownReturnCamera), stateAddress(TableId::CalbaniaTownScriptsTownReturnSteps));
            scriptReplayPadOn(); // exe 0x5117f0
            clearPlayerDirections();
            placePlayerWhole(int16_t(0xfb20), int16_t(0xfd80), 0x6a0, 0x280);
            s.subState = uint8_t(SetUpSub::TakeOver);
        } else if (SetUpSub(s.subState) != SetUpSub::TakeOver) {
            scriptSequenceTick();
            return;
        }
        if (playerToScriptIfFree() == 0) { // exe 0x572300
            startFaceTracks(stateAddress(TableId::CalbaniaTownScriptsTownReturnEyes), stateAddress(TableId::CalbaniaTownScriptsTownReturnMouth));
            s.enterState(CutsceneState::Steps);
        }
        scriptSequenceTick();
        return;
    case CutsceneState::Steps: townReturnSteps(s); return;
    case CutsceneState::WrapUp: townReturnWrapUp(s); return;
    default: notPortedAt(kStoryPort, "script 0x11 state", s.state);
    }
}

} // namespace od::port
