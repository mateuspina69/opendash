// The new-game opening's script (ST39, script 5; see opening.hpp): its states, its steps on the timeline and ST39's
// room case that starts it.
//   state 0 (exe 0x5151e0): the camera script / the steps, the replay pad, the player script driven, its face tracks,
//           the narration's voice clip (state 1 once it plays)
//   state 1 (exe 0x53a5e0): the generic step runner (game/script_engine)
//   state 2 (exe 0x515310): the wrap up: the request to ST1E (the Flutter) room 0
// Steps (exe 0x89e450; the step tick: ScriptState::stepTick):
//   0 0x515420 the sea, then the in-stage warp to room 1     1 0x5154b0 the crew (130 ticks)
//   2 0x515630 (60)  3 0x515660 the talk, the shake (1020)    4 0x515870 back to room 0
//   5 0x5159a0 to room 1                                      6 0x515a50 the long talk (until the narration ends)
//   7 0x515e40 the choice (message 0 of the ST39 bank)       8 0x516010 (0x140)
//   9 0x516230 the player walks off, the fade
#include "content/intro/opening_script_state.hpp"
#include "content/intro/opening.hpp"

#include "engine/data_source.hpp"
#include "game/room_scripts.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_sweeps.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/camera.hpp"
#include "game/camera_state.hpp"
#include "game/fade.hpp"
#include "game/fade_state.hpp"
#include "game/game_state.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/message.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/sky_layers.hpp"
#include "game/spawn.hpp"
#include "game/stage_files.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/transition.hpp"
#include "game/transition_state.hpp"

using namespace od::port::opening_script_state;

namespace od::port {

namespace {

constexpr uint8_t kOpeningScript = 5, kRevisitScript = 0x68;
constexpr uint8_t kST39 = 0x39, kST1E = 0x1e;
constexpr uint8_t kRevisitStory = 0x11, kRevisitRoom = 1;
constexpr uint32_t kRevisitFlag = 0x580; // set as the second visit's script starts
constexpr uint8_t kRoomCaseDone = 1;

// the running script's own bytes as script 5's
OpeningScriptBytes& openingBytes() { return scriptState().part<OpeningScriptBytes>(); }

// the states (exe table 0x89e740: a routine per state; the named routines of engine/routines.csv)
const DataTable<RoutineTable<3>> openingStates{TableId::OpeningScriptStates};
// the choice's voice clips (exe table 0x89e74c: a clip per choice)
const DataTable<ScalarTable<uint32_t, kOpeningChoices>> choiceVoices{TableId::OpeningScriptChoiceVoices};

constexpr uint32_t kNarration = 0x12; // the narration's voice clip
constexpr uint32_t kVoiceFade = 8;
constexpr uint16_t kEngineHumOn = 0x29f, kEngineHumOff = 0x2a0;
constexpr uint32_t kVibrationStop = 0x1000000;
constexpr uint8_t kSkyLayer = 0; // the sky layer the steps scroll
constexpr int8_t kChoiceWindow = 0;
constexpr int16_t kChoiceMessage = 0;
constexpr uint8_t kWalkOffFade = kFadeOutRequest | 2;

// the script's own state machine (state 0's sub-states)
enum class SetUpSub : uint8_t { Start = 0, TakeOver = 1, WaitFile = 2, WaitStream = 3, WaitVoice = 4 };
enum class WrapUpSub : uint8_t { Stop = 0, WaitVoice = 1, WaitRequest = 2 };

// the script block
ScriptState& block() { return scriptState(); }
// the player's face tracks: the eyes ...
KeyframeTrack& eyes() { return block().faceTracks.first; }
// ... and the mouth
KeyframeTrack& mouth() { return block().faceTracks.second; }
// the in-stage warp of the steps (the room of ST39 switched in place)
void warpToRoom(uint8_t room, int16_t x, int16_t z) {
    requestTransition(kTransitionWarp, kST39, room, x, 0, z, 0, kNoFade, kNoFade);
}
// the script actor k plays animation `anim` from key 0 (exe type 0x47)
void actorAnim(size_t k, uint8_t anim) {
    auto&& f = ScriptAnimatedFields(asAnimated(*block().actors[k]));
    f.anim = anim;
    f.key = 0;
}
constexpr size_t kTalkActor = 2, kSecondTalkActor = 7;
// the sky layer's horizontal offset and scroll speed
void skyScroll(int16_t offset, int16_t speed) {
    auto&& sky = skyLayers()[kSkyLayer];
    sky.horizontalOffset = offset;
    sky.scrollSpeedX = speed;
}

// ---- the steps
// -------------------------------------------------------------------------------------------------------

void seaStep() { // exe 0x515420 (until 0x235)
    const int32_t t = block().stepTick;
    if (t == 300) {
        skyScroll(0x280, 0);
    } else if (t == 0x235) {
        warpToRoom(1, 0, -0xb8);
        scriptNextStep();
    }
}

void crewStep() { // exe 0x5154b0 (130)
    const int32_t t = block().stepTick;
    if (t == 0) {
        actorsSweep(kSweepActors, kSweepFree);
        player().setDrawn(true);
        block().clearStepPhase();
        skyScroll(400, 0x180);
        soundPlayFlat(kEngineHumOn);
        openingBytes().engineHum = 1;
    }
    switch (t) {
    case 0:
        playerStageAnim(0);
        eyes().select(0, 0);
        mouth().select(0, 0);
        return;
    case 0x1e: eyes().select(1, 0); return;
    case 0x41:
        playerStageAnim(1);
        eyes().select(0, 0);
        actorAnim(kTalkActor, 1);
        return;
    case 0x6e: eyes().select(2, 0); return;
    default: return;
    }
}

void skyStep() { // exe 0x515630 (60)
    if (block().stepTick == 0) {
        skyLayers()[kSkyLayer].horizontalOffset = 0x26c;
        player().setDrawn(false);
    }
}

void shakeStep() { // exe 0x515660 (1020)
    if (block().stepTick == 0) {
        player().setDrawn(true);
        block().clearStepPhase();
    }
    const int32_t t = block().stepTick;
    switch (t) {
    case 0:
        playerStageAnim(2);
        eyes().select(3, 0);
        mouth().select(1, 0);
        break;
    case 100: mouth().select(2, 0); break;
    case 0xf0: mouth().select(1, 0); break;
    case 0x14a:
        playerStageAnim(3);
        eyes().select(4, 0);
        mouth().select(3, 0);
        actorAnim(kTalkActor, 2);
        cameraShake(false, 0x200, 0x10);
        padVibrate(0, 0x11820ff);
        break;
    case 0x15e:
        eyes().select(5, 0);
        mouth().select(4, 0);
        break;
    case 0x1b8:
        eyes().select(6, 0);
        mouth().select(5, 0);
        break;
    case 0x208: mouth().select(4, 0); break;
    case 0x334:
        playerStageAnim(4);
        eyes().select(7, 0);
        mouth().select(6, 0);
        break;
    case 0x398:
        playerStageAnim(5);
        eyes().select(8, 0);
        mouth().select(7, 0);
        break;
    default: break;
    }
    // the shake's roll: sin((t - 10) of 64 steps) * 128 >> 12 while 0x149 < t < 0x16b
    if (t > 0x149 && t < 0x16b)
        scriptCamera().roll = uint32_t((int32_t(fixedSin64(uint32_t(t - 10))) << 7) >> 12);
    else
        scriptCamera().roll = 0;
}

void backStep() { // exe 0x515870 (until 0x25d)
    switch (block().stepTick) {
    case 0:
        playerStageAnim(6);
        eyes().select(9, 0);
        mouth().select(9, 0);
        actorAnim(kTalkActor, 5);
        return;
    case 0xf0:
        eyes().select(10, 0);
        mouth().select(8, 0);
        return;
    case 0x186: mouth().select(9, 0); return;
    case 0x212:
        playerStageAnim(7);
        eyes().select(0xb, 0);
        mouth().select(10, 0);
        return;
    case 0x25d:
        warpToRoom(0, 0, 0);
        scriptNextStep();
        return;
    default: return;
    }
}

void returnStep() { // exe 0x5159a0 (until 0x78)
    const int32_t t = block().stepTick;
    if (t == 0) {
        player().setDrawn(false);
        skyScroll(0x244, -0x180);
        soundPlayFlat(kEngineHumOff);
        openingBytes().engineHum = 0;
        return;
    }
    if (t == 0x78) {
        warpToRoom(1, 0x40, -0xa0);
        scriptNextStep();
    }
}

// the long talk's phases: the lines on their ticks, the narration's end waited
enum class LongTalkPhase : uint8_t { Talk = 0, WaitVoice = 1 };

void longTalkStep() { // exe 0x515a50 (until the narration ends)
    ScriptState& s = block();
    if (LongTalkPhase(s.stepPhase) == LongTalkPhase::Talk) {
        switch (s.stepTick) {
        case 0:
            actorsSweep(kSweepActors, kSweepFree);
            player().setDrawn(true);
            playerStageAnim(8);
            eyes().select(0xc, 0);
            mouth().select(10, 0);
            soundPlayFlat(kEngineHumOn);
            openingBytes().engineHum = 1;
            return;
        case 0x186:
            playerStageAnim(9);
            eyes().select(0xb, 0);
            mouth().select(0xc, 0);
            return;
        case 0x208:
            playerStageAnim(10);
            eyes().select(0xb, 0);
            mouth().select(0xd, 0);
            return;
        case 0x2ee:
        case 0x30c: mouth().select(0xe, 0); return;
        case 0x302:
        case 0x316: mouth().select(0xf, 0); return;
        case 0x5dc:
            playerStageAnim(11);
            eyes().select(0xe, 0);
            mouth().select(0xb, 0);
            return;
        case 0x69a: playerStageAnim(12); return;
        case 0x753:
            playerStageAnim(13);
            eyes().select(0xc, 0);
            mouth().select(10, 0);
            return;
        case 0x780: actorAnim(kSecondTalkActor, 3); return;
        case 0x85c:
            playerStageAnim(14);
            eyes().select(0xc, 0);
            mouth().select(0x10, 0);
            return;
        case 0x866:
        case 0x88e: mouth().select(10, 0); return;
        case 0x870: mouth().select(0x10, 0); return;
        case 0x8f2: actorAnim(kSecondTalkActor, 4); return;
        case 0x906: playerStageAnim(15); return;
        case 0x974:
            if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade);
            s.flags |= kScriptSkipLocked;
            ++s.stepPhase;
            return;
        default: return;
        }
    }
    if (LongTalkPhase(s.stepPhase) == LongTalkPhase::WaitVoice && voiceClipIdle()) {
        scriptNextStepClearingPhases();
    }
}

// the choice's sub-phases (+7)
enum class ChoiceSubPhase : uint8_t { Ask = 0, WaitAnswer = 1, WaitFile = 2, WaitStream = 3, WaitVoice = 4, Acting = 5 };

// the choice step's phases: the walk to the choice, the choice (its sub-phases: ChoiceSubPhase)
enum class ChoicePhase : uint8_t { Walk = 0, Choose = 1 };

void choiceStep() { // exe 0x515e40
    ScriptState& s = block();
    OpeningScriptBytes& bytes = openingBytes();
    if (ChoicePhase(s.stepPhase) == ChoicePhase::Walk) {
        player().pos.x.whole = int16_t(int16_t(s.stepTick) + 0x40);
        if (s.stepTick == 0x10) {
            s.stepSubPhase = uint8_t(ChoiceSubPhase::Ask);
            bytes.choice = 0;
            ++s.stepPhase;
        }
        return;
    }
    if (ChoicePhase(s.stepPhase) != ChoicePhase::Choose) return;
    switch (ChoiceSubPhase(s.stepSubPhase)) {
    case ChoiceSubPhase::Ask:
        windowOpen(kChoiceWindow, kMessageBank, kChoiceMessage);
        ++s.stepSubPhase;
        [[fallthrough]];
    case ChoiceSubPhase::WaitAnswer:
        if (windowStatus(kChoiceWindow) != 0) return;
        for (uint8_t k = 0; k < kOpeningChoices; ++k)
            if (storyFlag(kOpeningChoiceFlags + k)) {
                bytes.choice = k;
                break;
            }
        ++s.stepSubPhase;
        return;
    case ChoiceSubPhase::WaitFile:
        if (!(fileLoads().loading & kFileLoading)) ++s.stepSubPhase;
        return;
    case ChoiceSubPhase::WaitStream:
        if (!(fileLoads().loading & kStreamBusy)) {
            voiceClipPlay(choiceVoices[bytes.choice]);
            ++s.stepSubPhase;
        }
        return;
    case ChoiceSubPhase::WaitVoice:
        if (voiceClipPlaying()) ++s.stepSubPhase;
        return;
    case ChoiceSubPhase::Acting:
        if (bytes.choice == kOpeningLastChoice) {
            if (!storyFlag(kOpeningChoiceFlags + kOpeningLastChoice)) scriptNextStep();
        } else if (!storyFlag(uint32_t(bytes.choice) + kOpeningChoiceFlags) && voiceClipIdle()) {
            s.stepSubPhase = uint8_t(ChoiceSubPhase::Ask);
        }
        return;
    default: return;
    }
}

// the after-choice step's phases: the walk back, the lines on their ticks, done
enum class AfterChoicePhase : uint8_t { Walk = 0, Talk = 1, Done = 2 };

void afterChoiceStep() { // exe 0x516010 (0x140)
    ScriptState& s = block();
    if (AfterChoicePhase(s.stepPhase) == AfterChoicePhase::Walk) {
        player().pos.x.whole = int16_t(0x50 - int16_t(s.stepTick));
        if (s.stepTick == 0x10) {
            s.flags &= uint8_t(~kScriptSkipLocked);
            ++s.stepPhase;
        }
        return;
    }
    if (AfterChoicePhase(s.stepPhase) != AfterChoicePhase::Talk) return;
    switch (s.stepTick) {
    case 0x28:
        playerStageAnim(16);
        eyes().select(0x10, 0);
        mouth().select(0xb, 0);
        return;
    case 0x50: mouth().select(0x14, 0); return;
    case 0x82: mouth().select(0xb, 0); return;
    case 0xe6:
        eyes().select(0x11, 0);
        mouth().select(0x15, 0);
        return;
    case 0x118:
        playerStageAnim(17);
        eyes().select(0x12, 0);
        mouth().select(0x11, 0);
        return;
    default: return;
    }
}

// the walk off's phases (+6) and its fade's (+7)
enum class WalkOffPhase : uint8_t { Talk = 0, Turn = 1, Walk = 2 };
constexpr int16_t kWalkSpeed = -0x200;
constexpr int32_t kWalkOffFadeTick = 0xfa;

// the walk off's fade (+7): until its tick, the fade waited
enum class WalkOffFadePhase : uint8_t { Wait = 0, Fading = 1 };

void walkOffStep() { // exe 0x516230 (until the fade)
    ScriptState& s = block();
    Player& p = player();
    const int32_t t = s.stepTick;
    if (t == 0) {
        s.clearStepPhaseBytes();
    }
    const WalkOffPhase phase = WalkOffPhase(s.stepPhase);
    if (phase == WalkOffPhase::Talk) {
        switch (t) {
        case 0:
            playerStageAnim(18);
            eyes().select(0x13, 0);
            mouth().select(0x12, 0);
            p.pos.x.whole = 0x50;
            p.rotation.y = 0x60;
            return;
        case 5: playerStageAnim(19); return;
        case 0x1e: playerStageAnim(20); return;
        case 0x28:
            playerStageAnim(21);
            eyes().select(0xc, 0);
            return;
        case 0x50: mouth().select(0x13, 0); return;
        case 0x78: mouth().select(0xd, 0); return;
        case 200:
            playerStageAnim(22);
            eyes().select(0xd, 0);
            mouth().select(0x10, 0);
            return;
        case 0xdc:
            playerStageAnim(23);
            mouth().select(10, 0);
            return;
        case 0xe0: s.stepPhase = uint8_t(WalkOffPhase::Turn); return;
        default: return;
        }
    }
    if (phase == WalkOffPhase::Turn) {
        playerScriptAnim(PlayerAnim::Run, 4, true);
        p.rotation.y += uint16_t(kHalfTurn);
        ++s.stepPhase;
    } else if (phase != WalkOffPhase::Walk) {
        return;
    }
    integrateVelocity(p, 0, 0, kWalkSpeed);
    if (WalkOffFadePhase(s.stepSubPhase) == WalkOffFadePhase::Wait) {
        if (s.stepTick == kWalkOffFadeTick) {
            scriptFade(kWalkOffFade, 0);
            s.flags |= kScriptUnskippable;
            ++s.stepSubPhase;
        }
    } else if (WalkOffFadePhase(s.stepSubPhase) == WalkOffFadePhase::Fading && fadeState().idle()) {
        scriptNextStepClearingPhases();
    }
}

// the steps' routines (by the named routine)
void openingStep(const RoutineRef& routine) {
    switch (routine.id()) {
    case RoutineId::OpeningSeaStep: seaStep(); return;
    case RoutineId::OpeningCrewStep: crewStep(); return;
    case RoutineId::OpeningSkyStep: skyStep(); return;
    case RoutineId::OpeningShakeStep: shakeStep(); return;
    case RoutineId::OpeningBackStep: backStep(); return;
    case RoutineId::OpeningReturnStep: returnStep(); return;
    case RoutineId::OpeningLongTalkStep: longTalkStep(); return;
    case RoutineId::OpeningChoiceStep: choiceStep(); return;
    case RoutineId::OpeningAfterChoiceStep: afterChoiceStep(); return;
    case RoutineId::OpeningWalkOffStep: walkOffStep(); return;
    default: notPortedAt(kStoryPort, "script 5 step function", routine.pointerValue());
    }
}

// ---- the states
// ------------------------------------------------------------------------------------------------------

void openingSetUp() { // exe 0x5151e0
    ScriptState& s = block();
    switch (SetUpSub(s.subState)) {
    case SetUpSub::Start:
        scriptSequenceStart(stateAddress(TableId::OpeningScriptCameraScript), stateAddress(TableId::OpeningScriptSteps));
        scriptReplayPadOn();
        skyLayers()[kSkyLayer].horizontalOffset = 600;
        player().setDrawn(false);
        skyLayers()[kSkyLayer].scrollSpeedX = -0x300;
        scriptSequenceTick();
        ++s.subState;
        [[fallthrough]];
    case SetUpSub::TakeOver: {
        if (playerToScriptIfFree() != 0) return;
        Player& p = player();
        p.pos.x = 0;
        p.pos.y = 0;
        p.pos.z = 0;
        p.clearDirectionOffsetWords();
        p.idleFaces[0] = 0;
        p.idleFaces[1] = 0;
        s.faceTracks.init(stateAddress(TableId::OpeningScriptFaceEyes), stateAddress(TableId::OpeningScriptFaceMouth));
        eyes().select(0, 0);
        mouth().select(0, 0);
        ++s.subState;
        return;
    }
    case SetUpSub::WaitFile:
        if (!(fileLoads().loading & kFileLoading)) ++s.subState;
        return;
    case SetUpSub::WaitStream:
        if (!(fileLoads().loading & kStreamBusy)) {
            voiceClipPlay(kNarration);
            ++s.subState;
        }
        return;
    case SetUpSub::WaitVoice:
        if (voiceClipPlaying()) s.enterState(CutsceneState::Steps);
        return;
    default: return;
    }
}

void openingWrapUp() { // exe 0x515310
    ScriptState& s = block();
    switch (WrapUpSub(s.subState)) {
    case WrapUpSub::Stop: {
        padVibrate(0, kVibrationStop);
        playerScriptAnim(PlayerAnim::Idle, 0, true);
        actorsSweep(kSweepActors, kSweepFree);
        windowCloseAll(WindowClose::Gentle);
        OpeningScriptBytes& bytes = openingBytes();
        if (bytes.engineHum != 0) {
            soundPlayFlat(kEngineHumOff);
            bytes.engineHum = 0;
        }
        if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade);
        ++s.subState;
        [[fallthrough]];
    }
    case WrapUpSub::WaitVoice:
        if (voiceClipIdle()) {
            ++s.subState;
            requestTransition(kTransitionAccepted, kST1E, 0, 0x300, -1, 0x300, kQuarterTurn, kFadeInRequest | 2,
                              kNoFade);
        }
        return;
    case WrapUpSub::WaitRequest:
        if (transitionRequest().state != kTransitionNone) return;
        playerBackToPlay(); // exe 0x5723c0
        scriptReplayPadOff();
        player().setDrawn(true);
        scriptEndBlock();
        return;
    default: return;
    }
}

// exe 0x5151c0: the state's routine (exe table 0x89e740; the named routines of engine/routines.csv)
void openingScript() {
    ScriptState& s = block();
    switch (openingStates->idAt(s.state)) {
    case RoutineId::OpeningSetUpState: openingSetUp(); return;
    case RoutineId::ScriptStepsState: scriptStepsState(&openingStep); return;
    case RoutineId::OpeningWrapUpState: openingWrapUp(); return;
    default: notPortedAt(kStoryPort, "script 5 state", s.state);
    }
}

} // namespace

bool openingScriptPorted(RoutineId handler) { return handler == RoutineId::OpeningScript; }

bool openingScriptTick(const PlayerEnv&, RoutineId handler) {
    if (handler != RoutineId::OpeningScript) return false;
    openingScript();
    return true;
}

bool openingRoomCaseKnown(uint8_t stage) { return stage == kST39; }

bool openingRoomCase() { // exe 0x587ba0
    const GameState& g = gameState();
    if (g.stage != kST39) return false;
    RoomScriptState& room = roomScriptState();
    if (room.handlerStep != 0) return true;
    if (g.story == 0) {
        if (g.room == 0) {
            scriptRun(kOpeningScript);
            room.handlerStep = kRoomCaseDone;
            return true;
        }
    } else if (g.story == kRevisitStory && g.room == kRevisitRoom) {
        setStoryFlag(kRevisitFlag);
        scriptRun(kRevisitScript);
    }
    room.handlerStep = kRoomCaseDone;
    return true;
}

} // namespace od::port
