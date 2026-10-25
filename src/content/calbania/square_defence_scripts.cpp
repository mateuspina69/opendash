// The scripts of the square's defence (ST1F sub 0 at story 8, the room handler 0x580ec0 starts them by the fight's
// flags 0x583..0x586):
//   0x1d (exe 0x520c70, states 0x8a3630: 0x520c90 / 0x53a5e0 / 0x520d80; camera 0x8a3448, steps 0x8a3580): flag 0x583,
//   the square -- its cast (type 0x43 sub 1, records 0x8a3420 / 0x8a3434) is the ST1F spawner's. State 0: the
//   sequence, the replay pad, unskippable, the face tracks 0x8a3618 / 0x8a3624 once script driven, voice 0; state 1
//   once it plays. Steps: 0x520e60 (the player at (-0x11e0, the ground, -0x648) yaw 0xc00, animations 0x80 / 0x81
//   (walks -0x50 a tick from 0x8c) / 0x82, next at 0xb8), 0x520f40 (animation 0x83 at 0x42, face keys, next at 0x10f),
//   0x5210a0 (animation 0x84 at 0x50, next at 0x1cd), 0x5210f0 (animation 0x85, face keys, next at 0x14), 0x5211c0
//   (animation 0x85, face keys; at 0x23 state 2 at once). State 2: vibration off, the voice faded, the two cast records
//   freed (fade 1 if covered), then the actors 0x496 swept and the player back.
//   0x1e (exe 0x5212a0, states 0x8a373c: 0x5212c0 / 0x53a5e0 / 0x521430; camera 0x8a3668, steps 0x8a36f8): phase 2
//   (flag 0x584). State 0: the sequence, the replay pad, the actors 0x496 op 5, the script actor of record 0x8a3640
//   (flag 0x581) or 0x8a3654, the player at (0x1450, -0x1ae, 0x640) rotation (0, 0xf88, 0), the music faded (0x111),
//   unskippable; the face tracks 0x8a3730 / 0x8a3738 once script driven; voice 2 (flag 0x581) or 3. Steps: 0x521540
//   (animation 0x80, the same place; next at 0xf), 0x5215c0 (animation 0x81 step 0x14 at 0x28, walks -0x50 a tick,
//   next at 0x78), 0x521620 (at (0x14d9, -0x1d3, 0x363) animation 0x81 step 0x1d, walks; animation 0x8a at 0x23, stops
//   at 0x28, animation 0x8b at 0x6e, state 2 at 0x10e; face keys). State 2: as 0x1d's (the record freed), then the
//   actors 0x496 op 10 and the player back at (0x1500, -0x1ff, 0x200) yaw 0.
//   0x1f (exe 0x521760, states 0x8a3930: 0x521780 / 0x53a5e0 / 0x521910; camera 0x8a3798, steps 0x8a3880): phase 3
//   (flag 0x585). State 0: the sequence, the replay pad, the actors 0x496 op 5, the script actors of records 0x8a3770 /
//   0x8a3784 (flag 0x580) or 0x8a3748 / 0x8a375c, the player at (0x14d1, -0x200, 0xa0) rotation 0, the music faded
//   (0x111), unskippable; the face tracks 0x8a390c / 0x8a3920; voice 6; animation 0x86 every tick of the state. Steps:
//   0x521a10 (the same place; next at 0x6e after 0x14), 0x521a70 (next at 0x6e), 0x521a90 (animation 0x87 at 10, face
//   keys, next at 0x3c), 0x521b30 (next at 0xa5), 0x521b50 (animation 0x88 at 0x32, face keys, state 2 at 0x78). State
//   2: as 0x1e's (both records freed), the actors 0x496 op 10, the player back where he is.
//   0x20 (exe 0x521c60, states 0x8a3abc: 0x521c80 / 0x521e20 / 0x521ea0; camera 0x8a3990, steps 0x8a3a70): the last
//   phase (flag 0x586). State 0: the sequence, the replay pad, the actors 0x496 swept, the script actors of records
//   0x8a3968 / 0x8a397c (flag 0x580) or 0x8a3940 / 0x8a3954, the player at (0x14f0, -0x200, -0x30) rotation 0, the music
//   faded (0x666), unskippable; the face tracks 0x8a3ab0 / 0x8a3ab8; voice 7; animation 0x89 every tick of the state.
//   The runner 0x521e20 also counts the lift after each step and, below 0x3c, lifts the player: y = -0x200 - [0x8a3ac8
//   + count * 2]. Steps: 0x521fc0 (the place, animation 0x89, airborne; next at 0x16), 0x522040 (moves -0x160 a tick
//   until 1, then the count 0xc + animation 0x89 step 0xc; next at 0x14), 0x5220b0 (moves; next at 0x37), 0x5220e0
//   (state 2 at 0x96). State 2: vibration off, the voice faded, fade 0x12 (unless skipped); once idle and the fade over
//   the actors 0xffe swept and request 2 -> ST30 sub 3 (0x243, -0x1b0, 0xb1a) yaw 0 (fade-in 2); then the player back.
#include "content/calbania/square_defence_scripts_state.hpp"
#include "content/calbania/ch3_scripts.hpp"

#include "engine/data_source.hpp"
#include "game/actor_sweeps.hpp"
#include "game/audio_logic.hpp"
#include "game/fade_state.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_motion.hpp"
#include "game/port_gap.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/transition.hpp"
#include "game/transition_state.hpp"

using namespace od::port::square_defence_scripts_state;

namespace od::port {

namespace {

enum class SetUpSub : uint8_t { Start = 0, TakeOver = 1, Voice = 2, WaitVoice = 3 };
enum class WrapUpSub : uint8_t { Stop = 0, Wait = 1, FreeCast = 2, End = 3 };

constexpr uint32_t kVibrationStop = 0x1000000;
constexpr uint32_t kVoiceFade = 8;
constexpr uint32_t kMusic = 0xf;
constexpr uint32_t kSweepCast = 0x496, kSweepKeep = 0x80, kSweepHold = 5, kSweepRelease = 10;
constexpr uint32_t kFlagDahAtGate = 0x580, kFlagAppoCarried = 0x581;

// the start shared by the scripts: unskippable until the voice plays
void lockSkip(ScriptState& s) { s.flags |= kScriptUnskippable; }
void unlockSkip(ScriptState& s) { s.flags &= uint8_t(~kScriptUnskippable); }
// the set-up's tail: the face tracks once script driven, the voice, state 1 once it plays
void startScene(ScriptState& s, uint32_t eyes, uint32_t mouth, uint32_t voice) {
    switch (SetUpSub(s.subState)) {
    case SetUpSub::TakeOver:
        if (playerToScriptIfFree() == 0) { // exe 0x572300
            startFaceTracks(eyes, mouth);  // exe 0x5e33a0
            ++s.subState;
        }
        return;
    case SetUpSub::Voice:
        voiceClipPlay(voice);
        ++s.subState;
        return;
    case SetUpSub::WaitVoice:
        if (voiceClipPlaying()) {
            s.enterState(CutsceneState::Steps);
            unlockSkip(s);
        }
        return;
    default: return;
    }
}
// the shared wrap-up of scripts 0x1d..0x1f: vibration off, the voice faded, the cast freed once no fade runs (fade 1
// when covered); true once the player can be put back
template <class FreeCast> bool squareWrapUp(ScriptState& s, FreeCast freeCast) {
    switch (WrapUpSub(s.subState)) {
    case WrapUpSub::Stop:
        padVibrate(0, kVibrationStop); // exe 0x55cd10
        if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade);
        ++s.subState;
        [[fallthrough]];
    case WrapUpSub::Wait:
        if (voiceClipIdle() && scriptState().part<SquareBytes>().phaseHold == 0) s.stepPhase = 0;
        s.subState = uint8_t(WrapUpSub::FreeCast);
        return false;
    case WrapUpSub::FreeCast:
        if (fadeState().busy == 0) {
            freeCast();
            if (fadeState().covered != 0) scriptFade(1, 0);
        }
        return false;
    case WrapUpSub::End: return true;
    default: return false;
    }
}

// ==== script 0x1d ========================================================================================================
constexpr uint32_t kOpeningVoice = 0;
constexpr RoutineId kStepArrive = RoutineId::SquareDefenceScriptsStepArrive,
                    kStepTalk = RoutineId::SquareDefenceScriptsStepTalk,
                    kStepListen = RoutineId::SquareDefenceScriptsStepListen,
                    kStepAnswer = RoutineId::SquareDefenceScriptsStepAnswer,
                   kStepLast = RoutineId::SquareDefenceScriptsStepLast;
enum class ArriveStep : uint8_t { Place = 0, Wait = 1, Walk = 2, Stop = 3 };
enum class TimedStep : uint8_t { Wait = 0, Run = 1 };
enum class FaceStep : uint8_t { Start = 0, Open = 1, Close = 2 };

// the face keys of the last two steps (on the second phase byte)
void openingFaceKeys(ScriptState& s) {
    const int32_t t = s.stepTick;
    switch (FaceStep(s.stepSubPhase)) {
    case FaceStep::Start:
        faceKeys(0);
        s.stepSubPhase = uint8_t(FaceStep::Open);
        [[fallthrough]];
    case FaceStep::Open:
        if (t == 1) {
            faceKeys(1);
            s.stepSubPhase = uint8_t(FaceStep::Close);
        }
        return;
    case FaceStep::Close:
        if (t != 0x10f) return;
        faceKeys(0);
        return;
    default: return;
    }
}
// exe 0x520d80
void openingWrapUp(ScriptState& s) {
    if (squareWrapUp(s, [&] {
            s.subState = uint8_t(WrapUpSub::End);
            scriptActorFreeRecord(stateAddress(TableId::SquareDefenceScriptsOpeningAppo)); // exe 0x511760
            scriptActorFreeRecord(stateAddress(TableId::SquareDefenceScriptsOpeningDah));
        }))
        if (transitionRequest().state == 0) {
            actorsSweep(kSweepCast, kSweepKeep);
            playerBackToPlay();   // exe 0x5723c0
            scriptReplayPadOff(); // exe 0x511890
            player().setDrawn(true);
            scriptEndBlock();
        }
}
enum class TalkFace : uint8_t { Start = 0, Eyes = 1, Mouth = 2, MouthShut = 3, MouthAgain = 4, MouthEnd = 5, Last = 6 };
void openingStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    const int32_t t = s.stepTick;
    switch (routine.id()) {
    case kStepArrive:
        switch (ArriveStep(s.subState)) {
        case ArriveStep::Place: {
            playerAnim(0x80);
            Player& p = player();
            p.pos.x.whole = int16_t(0xee20);
            p.pos.y.whole = int16_t(actorGround(scriptEnv(), p)); // exe 0x40f190
            p.pos.z.whole = int16_t(0xf9b8);
            p.rotation.y = int16_t(0xc00);
            s.subState = uint8_t(ArriveStep::Wait);
            [[fallthrough]];
        }
        case ArriveStep::Wait:
            if (t == 0x8c) {
                playerAnim(0x81, 0x18);
                s.subState = uint8_t(ArriveStep::Walk);
            }
            return;
        case ArriveStep::Walk:
            moveInFacing(player(), 0, 0, -0x50); // exe 0x571910
            if (t == 0xb4) {
                playerAnim(0x82);
                s.subState = uint8_t(ArriveStep::Stop);
            }
            return;
        case ArriveStep::Stop:
            if (t == 0xb8) nextStepFromSub();
            return;
        default: return;
        }
    case kStepTalk:
        if (TimedStep(s.subState) == TimedStep::Wait) {
            if (t == 0x42) {
                playerAnim(0x83);
                s.subState = uint8_t(TimedStep::Run);
            }
        } else if (TimedStep(s.subState) == TimedStep::Run && t == 0x10f) {
            nextStepFromSub();
            return;
        }
        switch (TalkFace(s.stepSubPhase)) {
        case TalkFace::Start:
            faceKeys(0);
            ++s.stepSubPhase;
            [[fallthrough]];
        case TalkFace::Eyes:
            if (t == 100) {
                eyesKey(1);
                ++s.stepSubPhase;
            }
            return;
        case TalkFace::Mouth:
            if (t == 0x8e) {
                mouthKey(1);
                ++s.stepSubPhase;
            }
            return;
        case TalkFace::MouthShut:
            if (t == 0xb4) {
                mouthKey(0);
                ++s.stepSubPhase;
            }
            return;
        case TalkFace::MouthAgain:
            if (t == 0xbe) {
                mouthKey(1);
                ++s.stepSubPhase;
            }
            return;
        case TalkFace::MouthEnd:
            if (t == 0x10e) {
                mouthKey(0);
                ++s.stepSubPhase;
            }
            return;
        case TalkFace::Last:
            if (t == 0x10f) faceKeys(0);
            return;
        default: return;
        }
    case kStepListen:
        if (TimedStep(s.subState) == TimedStep::Wait) {
            if (t == 0x50) {
                playerAnim(0x84);
                s.subState = uint8_t(TimedStep::Run);
            }
        } else if (TimedStep(s.subState) == TimedStep::Run && t == 0x1cd) {
            nextStepFromSub();
        }
        return;
    case kStepAnswer:
        if (TimedStep(s.subState) == TimedStep::Wait) {
            playerAnim(0x85);
            s.subState = uint8_t(TimedStep::Run);
        } else if (TimedStep(s.subState) == TimedStep::Run && t == 0x14) {
            nextStepFromSub();
            return;
        }
        openingFaceKeys(s);
        return;
    case kStepLast:
        if (TimedStep(s.subState) == TimedStep::Wait) {
            playerAnim(0x85);
            s.subState = uint8_t(TimedStep::Run);
        } else if (TimedStep(s.subState) != TimedStep::Run) {
            openingFaceKeys(s);
            return;
        }
        if (t == 0x23) {
            s.enterState(CutsceneState::WrapUp);
            openingWrapUp(s);
            return;
        }
        openingFaceKeys(s);
        return;
    default: notPortedAt(kStoryPort, "script 0x1d step", routine.pointerValue());
    }
}

} // namespace

void squareOpeningScript() {
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x520c90
        if (SetUpSub(s.subState) == SetUpSub::Start) {
            scriptSequenceStart(stateAddress(TableId::SquareDefenceScriptsOpeningCamera), stateAddress(TableId::SquareDefenceScriptsOpeningSteps));
            scriptReplayPadOn(); // exe 0x5117f0
            clearPlayerDirections();
            scriptSequenceTick();
            lockSkip(s);
            s.clearScriptByteRun();
            s.subState = uint8_t(SetUpSub::TakeOver);
        }
        startScene(s, stateAddress(TableId::SquareDefenceScriptsOpeningEyes), stateAddress(TableId::SquareDefenceScriptsOpeningMouth), kOpeningVoice);
        return;
    case CutsceneState::Steps: scriptStepsState(openingStep); return; // exe 0x53a5e0
    case CutsceneState::WrapUp: openingWrapUp(s); return;
    default: notPortedAt(kStoryPort, "script 0x1d state", s.state);
    }
}

namespace {

// ==== script 0x1e ========================================================================================================
constexpr RoutineId kStepGather = RoutineId::SquareDefenceScriptsStepGather,
                    kStepWalk = RoutineId::SquareDefenceScriptsStepWalk,
                    kStepCross = RoutineId::SquareDefenceScriptsStepCross;
enum class GatherStep : uint8_t { Place = 0, Wait = 1 };
enum class WalkStep : uint8_t { Wait = 0, Walk = 1 };
enum class CrossStep : uint8_t { Place = 0, Walk = 1, Slow = 2, Wait = 3, Last = 4 };
enum class CrossFace : uint8_t { Start = 0, Key = 1, Done = 2 };

// the script actor of the phase: by Appo carried away
uint32_t secondPhaseRecord() { return storyFlag(kFlagAppoCarried) ? stateAddress(TableId::SquareDefenceScriptsSecondWithAppo) : stateAddress(TableId::SquareDefenceScriptsSecondWithoutAppo); }
// the player at the phase's start place (the yaw 0xf88 with the pitch as one dword)
void secondPhasePlace() {
    Player& p = player();
    p.pos.x.whole = 0x1450;
    p.pos.y.whole = int16_t(0xfe52);
    p.pos.z.whole = 0x640;
    p.rotation.z = 0;
    p.rotation.setXY(0, 0xf88);
}
// exe 0x521430
void secondPhaseWrapUp(ScriptState& s) {
    if (!squareWrapUp(s, [&] {
            scriptActorFreeRecord(secondPhaseRecord()); // exe 0x511760
            s.subState = uint8_t(WrapUpSub::End);
        }))
        return;
    actorsSweep(kSweepCast, kSweepRelease);
    playerBackToPlay();   // exe 0x5723c0
    scriptReplayPadOff(); // exe 0x511890
    Player& p = player();
    p.pos.x.whole = 0x1500;
    p.setDrawn(true);
    p.pos.y.whole = int16_t(0xfe01);
    p.pos.z.whole = 0x200;
    p.rotation.y = 0;
    scriptEndBlock();
}
void secondPhaseStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    const int32_t t = s.stepTick;
    switch (routine.id()) {
    case kStepGather:
        if (GatherStep(s.stepPhase) == GatherStep::Place) {
            playerAnim(0x80);
            secondPhasePlace();
            s.stepPhase = uint8_t(GatherStep::Wait);
        } else if (GatherStep(s.stepPhase) != GatherStep::Wait) {
            return;
        }
        if (t == 0xf) scriptNextStep();
        return;
    case kStepWalk:
        if (WalkStep(s.stepPhase) == WalkStep::Wait) {
            if (t == 0x28) {
                playerAnim(0x81, 0x14);
                s.stepPhase = uint8_t(WalkStep::Walk);
            }
        } else if (WalkStep(s.stepPhase) == WalkStep::Walk) {
            moveInFacing(player(), 0, 0, -0x50);
            if (t == 0x78) scriptNextStep();
        }
        return;
    case kStepCross:
        switch (CrossStep(s.stepPhase)) {
        case CrossStep::Place: {
            Player& p = player();
            p.pos.x.whole = 0x14d9;
            p.pos.y.whole = int16_t(0xfe2d);
            p.pos.z.whole = 0x363;
            playerAnim(0x81, 0x1d);
            s.stepPhase = uint8_t(CrossStep::Walk);
            [[fallthrough]];
        }
        case CrossStep::Walk:
            moveInFacing(player(), 0, 0, -0x50);
            if (t == 0x23) {
                playerAnim(0x8a);
                s.stepPhase = uint8_t(CrossStep::Slow);
            }
            break;
        case CrossStep::Slow:
            moveInFacing(player(), 0, 0, -0x50);
            if (t == 0x28) s.stepPhase = uint8_t(CrossStep::Wait);
            break;
        case CrossStep::Wait:
            if (t == 0x6e) {
                playerAnim(0x8b);
                s.stepPhase = uint8_t(CrossStep::Last);
            }
            break;
        case CrossStep::Last:
            if (t == 0x10e) {
                s.enterState(CutsceneState::WrapUp);
                secondPhaseWrapUp(s);
                return;
            }
            break;
        default: break;
        }
        if (CrossFace(s.stepSubPhase) == CrossFace::Start) {
            faceKeys(0);
            s.stepSubPhase = uint8_t(CrossFace::Key);
        } else if (CrossFace(s.stepSubPhase) != CrossFace::Key) {
            return;
        }
        if (t == 0x6e) {
            eyesKey(1);
            s.stepSubPhase = uint8_t(CrossFace::Done);
        }
        return;
    default: notPortedAt(kStoryPort, "script 0x1e step", routine.pointerValue());
    }
}

} // namespace

void squareSecondPhaseScript() {
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x5212c0
        if (SetUpSub(s.subState) == SetUpSub::Start) {
            scriptSequenceStart(stateAddress(TableId::SquareDefenceScriptsSecondCamera), stateAddress(TableId::SquareDefenceScriptsSecondSteps));
            scriptReplayPadOn();
            actorsSweep(kSweepCast, kSweepHold);
            scriptActorSpawnRecord(secondPhaseRecord()); // exe 0x511740
            s.clearScriptByteRun();
            s.subState = uint8_t(SetUpSub::TakeOver);
            scriptState().part<SquareBytes>().unread4 = 0;
            clearPlayerDirections();
            secondPhasePlace();
            soundFadeOut(kMusic, 0x111, 0); // exe 0x5d5780
            scriptSequenceTick();
            lockSkip(s);
        }
        startScene(s, stateAddress(TableId::SquareDefenceScriptsSecondEyes), stateAddress(TableId::SquareDefenceScriptsSecondMouth), storyFlag(kFlagAppoCarried) ? 2 : 3);
        return;
    case CutsceneState::Steps: scriptStepsState(secondPhaseStep); return; // exe 0x53a5e0
    case CutsceneState::WrapUp: secondPhaseWrapUp(s); return;
    default: notPortedAt(kStoryPort, "script 0x1e state", s.state);
    }
}

namespace {

// ==== script 0x1f ========================================================================================================
constexpr uint32_t kThirdVoice = 6;
constexpr RoutineId kStepWatch = RoutineId::SquareDefenceScriptsStepWatch,
                    kStepPause = RoutineId::SquareDefenceScriptsStepPause,
                    kStepReply = RoutineId::SquareDefenceScriptsStepReply,
                    kStepHold = RoutineId::SquareDefenceScriptsStepHold,
                   kStepEnd = RoutineId::SquareDefenceScriptsStepEnd;
enum class WatchStep : uint8_t { Place = 0, Wait = 1, Next = 2 };
enum class EndFace : uint8_t { Start = 0, First = 1, Second = 2, Third = 3, Done = 4 };

// the phase's two script actors (by Dah at the gate first): spawned or freed
void thirdPhaseRecords(bool spawn) {
    const bool f = storyFlag(kFlagDahAtGate);
    const uint32_t r0 = f ? stateAddress(TableId::SquareDefenceScriptsThirdPhaseDahFirstRecord) : stateAddress(TableId::SquareDefenceScriptsThirdPhaseFirstRecord), r1 = f ? stateAddress(TableId::SquareDefenceScriptsThirdPhaseDahSecondRecord) : stateAddress(TableId::SquareDefenceScriptsThirdPhaseSecondRecord);
    if (spawn) {
        scriptActorSpawnRecord(r0);
        scriptActorSpawnRecord(r1);
    } else {
        scriptActorFreeRecord(r0);
        scriptActorFreeRecord(r1);
    }
}
void thirdPhasePlace() {
    Player& p = player();
    p.pos.x.whole = 0x14d1;
    p.pos.y.whole = int16_t(0xfe00);
    p.pos.z.whole = 0xa0;
    p.rotation.setXY(0, 0);
    p.rotation.z = 0;
}
// exe 0x521910
void thirdPhaseWrapUp(ScriptState& s) {
    if (!squareWrapUp(s, [&] {
            thirdPhaseRecords(false);
            s.subState = uint8_t(WrapUpSub::End);
        }))
        return;
    actorsSweep(kSweepCast, kSweepRelease);
    playerBackToPlay();   // exe 0x5723c0
    scriptReplayPadOff(); // exe 0x511890
    player().setDrawn(true);
    scriptEndBlock();
}
void thirdPhaseStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    const int32_t t = s.stepTick;
    switch (routine.id()) {
    case kStepWatch:
        if (WatchStep(s.stepPhase) == WatchStep::Place) {
            thirdPhasePlace();
            s.stepPhase = uint8_t(WatchStep::Wait);
        } else if (WatchStep(s.stepPhase) != WatchStep::Wait) {
            if (WatchStep(s.stepPhase) == WatchStep::Next && t == 0x6e) scriptNextStep();
            return;
        }
        if (t == 0x14) s.stepPhase = uint8_t(WatchStep::Next);
        return;
    case kStepPause:
        if (t == 0x6e) scriptNextStep();
        return;
    case kStepReply:
        if (TimedStep(s.stepPhase) == TimedStep::Wait) {
            if (t == 10) {
                playerAnim(0x87);
                s.stepPhase = uint8_t(TimedStep::Run);
            }
        } else if (TimedStep(s.stepPhase) == TimedStep::Run && t == 0x3c) {
            scriptNextStep();
            return;
        }
        if (CrossFace(s.stepSubPhase) == CrossFace::Start) {
            faceKeys(0);
            s.stepSubPhase = uint8_t(CrossFace::Key);
        } else if (CrossFace(s.stepSubPhase) != CrossFace::Key) {
            return;
        }
        if (t == 10) {
            eyesKey(1);
            s.stepSubPhase = uint8_t(CrossFace::Done);
        }
        return;
    case kStepHold:
        if (t == 0xa5) scriptNextStep();
        return;
    case kStepEnd:
        if (TimedStep(s.stepPhase) == TimedStep::Wait) {
            if (t == 0x32) {
                playerAnim(0x88);
                s.stepPhase = uint8_t(TimedStep::Run);
            }
        } else if (TimedStep(s.stepPhase) == TimedStep::Run && t == 0x78) {
            s.enterState(CutsceneState::WrapUp);
            thirdPhaseWrapUp(s);
            return;
        }
        switch (EndFace(s.stepSubPhase)) {
        case EndFace::Start:
            eyesKey(2);
            mouthKey(0);
            s.stepSubPhase = uint8_t(EndFace::First);
            [[fallthrough]];
        case EndFace::First:
            if (t == 10) {
                eyesKey(3);
                mouthKey(1);
                s.stepSubPhase = uint8_t(EndFace::Second);
            }
            return;
        case EndFace::Second:
            if (t == 0x32) {
                eyesKey(4);
                mouthKey(2);
                s.stepSubPhase = uint8_t(EndFace::Third);
            }
            return;
        case EndFace::Third:
            if (t == 0x50) {
                mouthKey(3);
                s.stepSubPhase = uint8_t(EndFace::Done);
            }
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x1f step", routine.pointerValue());
    }
}

} // namespace

void squareThirdPhaseScript() {
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x521780
        if (SetUpSub(s.subState) == SetUpSub::Start) {
            scriptSequenceStart(stateAddress(TableId::SquareDefenceScriptsThirdCamera), stateAddress(TableId::SquareDefenceScriptsThirdSteps));
            scriptReplayPadOn();
            actorsSweep(kSweepCast, kSweepHold);
            thirdPhaseRecords(true); // exe 0x511740 x 2
            s.clearScriptByteRun();
            s.subState = uint8_t(SetUpSub::TakeOver);
            clearPlayerDirections();
            thirdPhasePlace();
            soundFadeOut(kMusic, 0x111, 0);
            scriptSequenceTick();
            lockSkip(s);
        }
        startScene(s, stateAddress(TableId::SquareDefenceScriptsThirdEyes), stateAddress(TableId::SquareDefenceScriptsThirdMouth), kThirdVoice);
        playerAnim(0x86);
        return;
    case CutsceneState::Steps: scriptStepsState(thirdPhaseStep); return; // exe 0x53a5e0
    case CutsceneState::WrapUp: thirdPhaseWrapUp(s); return;
    default: notPortedAt(kStoryPort, "script 0x1f state", s.state);
    }
}

namespace {

// ==== script 0x20 ========================================================================================================
constexpr uint32_t kLiftVoice = 7;
constexpr RoutineId kStepRunUp = RoutineId::SquareDefenceScriptsStepRunUp,
                    kStepThrow = RoutineId::SquareDefenceScriptsStepThrow,
                    kStepFly = RoutineId::SquareDefenceScriptsStepFly,
                    kStepLand = RoutineId::SquareDefenceScriptsStepLand;
const DataTable<ScalarTable<int16_t, 0x3c>> liftHeights{TableId::SquareDefenceScriptsLiftHeights}; // the player's height over -0x200 by the lift's count
constexpr uint8_t kLiftTicks = 0x3c, kLiftThrown = 0xc;
enum class RunUpStep : uint8_t { Place = 0, Wait = 1 };
enum class ThrowStep : uint8_t { Run = 0, Thrown = 1 };
enum class LiftWrapUp : uint8_t { Stop = 0, WaitVoice = 1, Leave = 2, End = 3 };
constexpr uint32_t kSweepAll = 0xffe;

// the script's records: by Dah at the gate first
void liftRecords() {
    const bool f = storyFlag(kFlagDahAtGate);
    scriptActorSpawnRecord(f ? stateAddress(TableId::SquareDefenceScriptsLiftDahFirstRecord) : stateAddress(TableId::SquareDefenceScriptsLiftFirstRecord));
    scriptActorSpawnRecord(f ? stateAddress(TableId::SquareDefenceScriptsLiftDahSecondRecord) : stateAddress(TableId::SquareDefenceScriptsLiftSecondRecord));
}
void liftPlace() {
    Player& p = player();
    p.pos.x.whole = 0x14f0;
    p.pos.y.whole = int16_t(0xfe00);
    p.pos.z.whole = int16_t(0xffd0);
    p.rotation.setXY(0, 0);
    p.rotation.z = 0;
}
// exe 0x521ea0: the voice faded, fade 0x12 (unless skipped); the request to ST30 sub 3; the player back
void liftWrapUp(ScriptState& s) {
    switch (LiftWrapUp(s.subState)) {
    case LiftWrapUp::Stop:
        padVibrate(0, kVibrationStop);
        if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade);
        ++s.subState;
        if (!(s.flags & kScriptSkipTaken)) scriptFade(0x12, 0);
        [[fallthrough]];
    case LiftWrapUp::WaitVoice:
        if (voiceClipIdle()) ++s.subState;
        return;
    case LiftWrapUp::Leave:
        if (fadeState().busy == 0) {
            actorsSweep(kSweepAll, kSweepKeep);
            s.subState = uint8_t(LiftWrapUp::End);
            requestTransition(kTransitionAccepted, 0x30, 3, 0x243, int16_t(0xfe50), 0xb1a, 0, 2, kNoFade);
        }
        return;
    case LiftWrapUp::End:
        if (transitionRequest().state == 0) {
            playerBackToPlay();   // exe 0x5723c0
            scriptReplayPadOff(); // exe 0x511890
            player().setDrawn(true);
            scriptEndBlock();
        }
        return;
    default: return;
    }
}
void liftStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    const int32_t t = s.stepTick;
    switch (routine.id()) {
    case kStepRunUp:
        if (RunUpStep(s.stepPhase) == RunUpStep::Place) {
            liftPlace();
            playerAnim(0x89);
            player().airborne = 1;
            s.stepPhase = uint8_t(RunUpStep::Wait);
        } else if (RunUpStep(s.stepPhase) != RunUpStep::Wait) {
            return;
        }
        if (t == 0x16) scriptNextStep();
        return;
    case kStepThrow:
        if (ThrowStep(s.stepPhase) == ThrowStep::Run) {
            moveInFacing(player(), 0, 0, -0x160);
            if (t == 1) s.stepPhase = uint8_t(ThrowStep::Thrown);
        } else if (ThrowStep(s.stepPhase) == ThrowStep::Thrown) {
            scriptState().part<SquareBytes>().liftCount = kLiftThrown;
            playerAnim(0x89, kLiftThrown);
        }
        if (t == 0x14) scriptNextStep();
        return;
    case kStepFly:
        moveInFacing(player(), 0, 0, -0x160);
        if (t == 0x37) scriptNextStep();
        return;
    case kStepLand:
        if (t == 0x96) {
            s.enterState(CutsceneState::WrapUp);
            liftWrapUp(s);
        }
        return;
    default: notPortedAt(kStoryPort, "script 0x20 step", routine.pointerValue());
    }
}
// exe 0x521e20's step: the step's routine, then the lift's count and (below 0x3c) the player's height
void liftStepAndLift(const RoutineRef& routine) {
    liftStep(routine);
    ScriptState& s = scriptState();
    SquareBytes& bytes = scriptState().part<SquareBytes>();
    const uint8_t c = uint8_t(bytes.liftCount + 1u);
    bytes.liftCount = c;
    if (c < kLiftTicks) player().pos.y.whole = int16_t(-0x200 - liftHeights[c]);
}

} // namespace

void squareWallLiftScript() {
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x521c80
        if (SetUpSub(s.subState) == SetUpSub::Start) {
            scriptSequenceStart(stateAddress(TableId::SquareDefenceScriptsLiftCamera), stateAddress(TableId::SquareDefenceScriptsLiftSteps));
            scriptReplayPadOn();
            actorsSweep(kSweepCast, kSweepKeep);
            liftRecords(); // exe 0x511740 x 2
            s.clearScriptByteRun();
            s.subState = uint8_t(SetUpSub::TakeOver);
            SquareBytes& bytes = scriptState().part<SquareBytes>();
            bytes.unread4 = 0;
            bytes.liftCount = 0;
            clearPlayerDirections();
            liftPlace();
            soundFadeOut(kMusic, 0x666, 0);
            lockSkip(s);
            scriptSequenceTick();
        }
        startScene(s, stateAddress(TableId::SquareDefenceScriptsLiftEyes), stateAddress(TableId::SquareDefenceScriptsLiftMouth), kLiftVoice);
        playerAnim(0x89);
        return;
    case CutsceneState::Steps: scriptStepsState(liftStepAndLift); return; // exe 0x521e20
    case CutsceneState::WrapUp: liftWrapUp(s); return;
    default: notPortedAt(kStoryPort, "script 0x20 state", s.state);
    }
}

} // namespace od::port
