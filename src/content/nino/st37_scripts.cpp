// The Nino ruin's ST37 scripts, one routine per exe routine; the names are hypotheses from the observed behaviour.
//   0x5f (exe 0x536ec0, states 0x8abfa8: 0x536ee0 set-up, the steps runner with the head turn exe 0x531310, 0x537030
//        wrap-up): ST37 sub 6 at story 10 (handler 0x5873c0, flag 0x590), the boss room's opening. Flooded (the player
//        in water): the camera 0x8abdbc / the steps 0x8abf20 and the cast records 0x8abd40 / 0x8abd54 / 0x8abd68, else
//        0x8abe6c / 0x8abf40 and 0x8abd80 / 0x8abd94 / 0x8abda8 (the camera script spawns them: pool 0x20 type 0x76
//        sub 2, content/nino/gauge_reaverbots). The set-up: the player at (-0x920, 0, 0) facing 0xc00, the music
//        faded, the face tracks 0x8abf98 / 0x8abfa0 once script driven, the voice clip 0x12 once the loader is idle,
//        the steps once it plays. The steps: 0x5371a0 (the player walks in: animation 1, -0x200 a tick; at 0x14
//        animation 0, next), 0x537150 / 0x537210 (next at 0xdc / 0xc8), 0x537180 / 0x537240 (at 0x50 the wrap-up at
//        once). The wrap-up: the vibration off, the voice faded, the fade 0x11 (unless skipped), the cast freed, the
//        player back, the sound 0x36 (the fight's music).
#include "content/nino/st37_scripts.hpp"

#include "engine/routine_ids.hpp"
#include "game/audio_logic.hpp"
#include "game/fade_state.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/message.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/stage_files.hpp"
#include "game/story.hpp"
#include "game/transition.hpp"

namespace od::port {

namespace {

constexpr TableId kFloodedCast[] = {TableId::St37ScriptsFloodedCast0, TableId::St37ScriptsFloodedCast1, TableId::St37ScriptsFloodedCast2};
constexpr TableId kDryCast[] = {TableId::St37ScriptsDryCast0, TableId::St37ScriptsDryCast1, TableId::St37ScriptsDryCast2};
constexpr int16_t kStartX = -0x920, kFacing = 0xc00;
constexpr int16_t kMusicFadeSlow = 0x222;
constexpr uint32_t kVoiceClip = 0x12, kVoiceFade = 8;
constexpr uint32_t kEndFade = 0x11, kUncoverFade = 1;
constexpr uint16_t kFightMusic = 0x36;
constexpr int16_t kWalkInSpeed = -0x200;

enum class SetUp : uint8_t { Start = 0, TakeOver = 1, WaitLoad = 2, Voice = 3, WaitVoice = 4 };
enum class WrapUp : uint8_t { Start = 0, FadeVoice = 1, FreeCast = 2, End = 3 };
enum class WalkInPhase : uint8_t { Start = 0, Walk = 1 };

void nextStep() { // the step's phase bytes cleared, exe 0x5115c0(1)
    scriptState().clearStepPhase();
    scriptSequenceSkip(1);
}
// exe 0x537030: the vibration off, the voice faded, the fade (unless skipped); once the fade is idle the windows closed,
// the cast freed (the screen uncovered); the player back facing 0xc00, shown, the block ended, the fight's music
void wrapUp() {
    ScriptState& s = scriptState();
    switch (WrapUp(s.subState)) {
    case WrapUp::Start:
        padVibrate(0, 0x1000000);                             // exe 0x55cd10
        if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade); // exe 0x601220
        s.subState += 1u;
        if (!(s.flags & kScriptSkipTaken)) scriptFade(kEndFade, 0); // exe 0x53f6b0
        [[fallthrough]];
    case WrapUp::FadeVoice:
        if (voiceClipIdle()) s.subState += 1u; // exe 0x601260
        return;
    case WrapUp::FreeCast:
        if (fadeState().busy != 0) return;
        windowCloseAll(WindowClose::Gentle); // exe 0x5577d0
        {
            const TableId* cast = player().inWater == 1 ? kFloodedCast : kDryCast;
            for (size_t i = 0; i < 3; ++i) scriptActorFreeRecord(stateAddress(cast[i])); // exe 0x511760
        }
        s.subState = uint8_t(WrapUp::End);
        if (fadeState().covered != 0) scriptFade(kUncoverFade, 0);
        return;
    case WrapUp::End: {
        Player& p = player();
        p.rotation.y = kFacing;
        playerBackToPlay();   // exe 0x5723c0
        scriptReplayPadOff(); // exe 0x511890
        p.setDrawn(true);
        scriptEndBlock();                 // exe 0x5116a0
        soundPlayFlatForced(kFightMusic); // exe 0x5d5260
        return;
    }
    default: return;
    }
}
void openingStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    const int32_t t = s.stepTick;
    switch (routine.id()) {
    case RoutineId::St37OpeningStep1: // the player walks in
        switch (WalkInPhase(s.stepPhase)) {
        case WalkInPhase::Start:
            playerScriptAnimId(1, 0, 1); // exe 0x5723d0(1, 0, 1)
            s.stepPhase += 1u;
            [[fallthrough]];
        case WalkInPhase::Walk:
            moveInFacing(player(), 0, 0, kWalkInSpeed); // exe 0x571910
            if (s.stepTick == 0x14) {
                playerScriptAnimId(0, 0, 1);
                nextStep();
            }
            return;
        default: return;
        }
    case RoutineId::St37OpeningStep2:
        if (s.stepPhase == 0 && t == 0xdc) nextStep();
        return;
    case RoutineId::St37OpeningStep3:
        if (s.stepPhase == 0 && t == 0xc8) nextStep();
        return;
    case RoutineId::St37OpeningStep4:
    case RoutineId::St37OpeningStep5:
        if (s.stepPhase == 0 && t == 0x50) {
            s.enterState(CutsceneState::WrapUp);
            wrapUp();
        }
        return;
    default: notPortedAt(kStoryPort, "script 0x5f step", routine.pointerValue());
    }
}
// exe 0x536ee0: the camera and the steps of the flooded or the dry room, the replay pad, the player placed, the music
// faded; once the player is script driven the face tracks; the voice clip once the loader is idle; the steps once it
// plays
void setUp() {
    ScriptState& s = scriptState();
    switch (SetUp(s.subState)) {
    case SetUp::Start: {
        Player& p = player();
        if (p.inWater == 1) scriptSequenceStart(stateAddress(TableId::St37ScriptsFloodedCamera), scriptStepList<TableId::St37ScriptsFloodedSteps>); // exe 0x511390
        else scriptSequenceStart(stateAddress(TableId::St37ScriptsDryCamera), scriptStepList<TableId::St37ScriptsDrySteps>);
        scriptReplayPadOn(); // exe 0x5117f0
        p.setPrevMoveDir(MoveDir::Forward);
        p.setMoveDir(MoveDir::Forward);
        p.clearDirectionOffsetWords();
        p.pos.x.whole = kStartX;
        p.pos.y.whole = 0;
        p.pos.z.whole = 0;
        p.rotation.y = kFacing;
        s.clearScriptByteRun(); // exe 0x40dc40(+0xc, +0x18)
        s.subState = uint8_t(SetUp::TakeOver);
        scriptSequenceTick();                          // exe 0x5114d0
        soundFadeOut(kMusicFades, kMusicFadeSlow, 0); // exe 0x5d5780
        [[fallthrough]];
    }
    case SetUp::TakeOver:
        if (playerToScriptIfFree() != 0) return; // exe 0x572300
        s.faceTracks.init(stateAddress(TableId::St37ScriptsEyes), stateAddress(TableId::St37ScriptsMouth));        // exe 0x5e33a0
        s.faceTracks.first.select(0, 0);         // exe 0x5e33d0
        s.faceTracks.second.select(0, 0);        // exe 0x5e33f0
        s.subState += 1u;
        return;
    case SetUp::WaitLoad:
        if (!(fileLoads().loading & kFileLoading)) s.subState += 1u;
        return;
    case SetUp::Voice:
        if (fileLoads().loading & kStreamBusy) return;
        voiceClipPlay(kVoiceClip); // exe 0x601050
        s.subState += 1u;
        return;
    case SetUp::WaitVoice:
        if (voiceClipPlaying()) s.enterState(CutsceneState::Steps); // exe 0x601240
        return;
    default: return;
    }
}
// exe 0x536ec0
void opening() {
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) { // [0x8abfa8]
    case CutsceneState::SetUp: setUp(); return;
    case CutsceneState::Steps: scriptStepsStateHeadTurn(&openingStep); return; // exe 0x531310
    case CutsceneState::WrapUp: wrapUp(); return;
    default: notPortedAt(kStoryPort, "script 0x5f state", s.state);
    }
}

} // namespace

bool st37ScriptPorted(RoutineId handler) { return handler == RoutineId::St37OpeningScript; }
bool st37ScriptTick(const PlayerEnv&, RoutineId handler) {
    if (handler != RoutineId::St37OpeningScript) return false;
    opening();
    return true;
}

} // namespace od::port
