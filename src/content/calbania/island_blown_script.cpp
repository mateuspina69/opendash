// Script 0x3c (exe 0x52b3b0, states 0x8a75b8: 0x52b3d0 / 0x52b4f0 / 0x52b550; camera 0x8a74ac, steps 0x8a7590): ST46
// sub 3 (after event 0x3b: Glyde's last attack lost, the machine destroyed) -- the island blown up, then the game over.
// State 0: the actors 0xffe swept, the sequence, the replay pad, unskippable, the script's bytes cleared, the face
// words cleared, the player at (0x100, 0, -0xb0) yaw 0 hidden, sound 0xd; once script driven animation 0, voice 1;
// state 1 (the cast started) once it plays. Steps: 0x51d380 (animation 0 once), 0x52ef00 (nothing), 0x52b670 (at 0x13
// the warp to ST46 sub 1 at (0x20, -0xd60, 0x134) yaw 0xc00, the white screen off), 0x52b6e0 (at 0x15 / 0x21 four
// blasts (records 0x8a75c8), at 0x16 / 0x1c / 0x22 a camera shake (1, 0x800, 0x20), at 0x1b fade 0x1a, unskippable,
// the white screen on and the blasts). The runner 0x52b4f0: skip -> fade 0x1a, state 2; the end of the steps -> state
// 2 at once. State 2: the music faded (0x222), the voice faded; once both are done fade 0x12; once covered the player
// back, the block ended and the game over: the main task's mode ToGameOver and the task switch to 0x542ff0 (exe
// 0x604b06; the app's title flow reacts to the mode -> the game-over picture -> continue from the save). The white
// screen (exe 0x5f30e0) is the host's: islandBlownWhiteScreen.
#include "content/calbania/ch3_scripts.hpp"
#include "content/calbania/ch3_story.hpp"

#include "engine/data_source.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/audio_logic.hpp"
#include "game/camera.hpp"
#include "game/display.hpp"
#include "game/explosions.hpp"
#include "game/fade_state.hpp"
#include "game/global_class_types.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"
#include "game/table_records.hpp"
#include "game/title.hpp"
#include "game/transition.hpp"
#include "game/transition_state.hpp"

namespace od::port {

namespace {

enum class SetUpSub : uint8_t { Start = 0, TakeOver = 1, Voice = 2, WaitVoice = 3 };
enum class WrapUpSub : uint8_t { Fade = 0, WaitSound = 1, FadeOut = 2, End = 3, EndAgain = 4 };
constexpr RoutineId kStepStill = RoutineId::St24GateStep1, kStepNothing = RoutineId::EmptyRoutine,
                    kStepWarp = RoutineId::IslandBlownScriptStepWarp,
                    kStepBlasts = RoutineId::IslandBlownScriptStepBlasts;
constexpr uint32_t kSweepAll = 0xffe, kSweepKeep = 0x80;
constexpr uint32_t kVoiceFade = 8;
constexpr uint32_t kBlowUpFade = 0x1a;
constexpr uint8_t kIslandBlownScript = 0x3c;
constexpr uint8_t kWhiteFromSub = uint8_t(WrapUpSub::End); // the wrap-up's sub-state from which a fade keeps it

// the blasts' records (exe data, 8 bytes each): {x, y, z, size}
struct BlastRecord : DataRecord {
    ShortPoint point;
    uint16_t size = 0;
    static BlastRecord load(const TableReader& r) {
        BlastRecord rec;
        rec.point = ShortPoint::load(r.sub(0x0, 6));
        rec.size = r.u16(0x6);
        return rec;
    }
};
const DataTable<RecordTable<BlastRecord, 4, 8>> blastRecords{TableId::IslandBlownScriptBlastRecords};
constexpr uint32_t kShakeAmount = 0x800;
constexpr uint16_t kShakeDecay = 0x20;

// exe 0x52b71b: the four blasts
void islandBlasts() {
    for (size_t r = 0; r < blastRecords.size(); ++r) {
        const uint32_t address = allocActor60(0); // exe 0x5e1300
        if (address == 0) continue;
        const BlastRecord rec = blastRecords[r];
        StaticActor& a = staticActorAt(address);
        a.flags = kActorLive;
        a.type = uint8_t(GlobalStaticType::Blast);
        a.subtype = 0;
        a.variant = 0;
        a.loopFlags |= kActorSweepGroup2;
        a.pos.x.whole = rec.point.x;
        a.pos.y.whole = rec.point.y;
        a.pos.z.whole = rec.point.z;
        BlastParams params(actorHeaderAt(address));
        params.size = rec.size;
        params.attack = 0;
    }
}
void islandBlownStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    const int32_t t = s.stepTick;
    switch (routine.id()) {
    case kStepStill:
        if (s.stepPhase == 0) {
            playerAnim(0); // exe 0x5723d0(0, 0, 1)
            ++s.stepPhase;
        }
        return;
    case kStepNothing: return;
    case kStepWarp:
        if (t == 0x13) {
            requestTransition(kTransitionWarp, 0x46, 1, 0x20, int16_t(0xf2a0), 0x134, 0xc00, kNoFade, kNoFade);
            scriptState().part<IslandBlownBytes>().whiteScreen = 0;
        }
        return;
    case kStepBlasts:
        switch (t) { // the byte table 0x52b7ac over t - 0x15
        case 0x1b:
            scriptFade(kBlowUpFade, 0);
            s.flags |= kScriptUnskippable;
            scriptState().part<IslandBlownBytes>().whiteScreen = 1;
            [[fallthrough]];
        case 0x15:
        case 0x21: islandBlasts(); break;
        case 0x16:
        case 0x1c:
        case 0x22: cameraShake(true, kShakeAmount, kShakeDecay); break; // exe 0x40ea80
        default: break;
        }
        return;
    default: notPortedAt(kStoryPort, "script 0x3c step", routine.pointerValue());
    }
}
// exe 0x52b550
void islandBlownWrapUp(ScriptState& s) {
    switch (WrapUpSub(s.subState)) {
    case WrapUpSub::Fade:
        soundFadeOut(kMusicFades, 0x222, 0);                  // exe 0x5d5780
        if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade); // exe 0x601220
        ++s.subState;
        [[fallthrough]];
    case WrapUpSub::WaitSound:
        if (voiceClipIdle() && soundDone(kMusicFades)) ++s.subState; // exe 0x601260 / 0x5d5900
        return;
    case WrapUpSub::FadeOut:
        if (fadeState().busy != 0) return;
        scriptFade(0x12, 0);
        ++s.subState;
        return;
    case WrapUpSub::End:
    case WrapUpSub::EndAgain:
        if (fadeState().busy != 0) return;
        playerBackToPlay();   // exe 0x5723c0
        scriptReplayPadOff(); // exe 0x511890
        player().airborne = 0;
        player().setDrawn(true);
        scriptEndBlock(); // exe 0x5116a0
        // exe 0x604b06(0x542ff0) with the game-over mode: the game task ends here (the app's title flow takes the frame
        // back on the mode's change: the game-over picture, then the save reload)
        mainTask().enterMode(MainMode::ToGameOver, 1);
        return;
    default: return;
    }
}

} // namespace

void islandBlownScript() {
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x52b3d0
        switch (SetUpSub(s.subState)) {
        case SetUpSub::Start:
            actorsSweep(kSweepAll, kSweepKeep);  // exe 0x5e05d0
            scriptSequenceStart(stateAddress(TableId::IslandBlownScriptBlownCamera), stateAddress(TableId::IslandBlownScriptSteps)); // exe 0x511390
            scriptReplayPadOn();                  // exe 0x5117f0
            s.flags |= kScriptUnskippable;
            clearPlayerAnglesAndFaces();
            placePlayer(0x1000000, 0, 0xff500000u, 0);
            player().setDrawn(false);
            s.clearScriptByteRun();    // exe 0x40dc40
            scriptSequenceTick();      // exe 0x5114d0
            soundPlayFlatForced(0xd);  // exe 0x5d5260
            ++s.subState;
            [[fallthrough]];
        case SetUpSub::TakeOver:
            if (playerToScriptIfFree() != 0) return; // exe 0x572300
            playerAnim(0);
            ++s.subState;
            return;
        case SetUpSub::Voice:
            voiceClipPlay(1); // exe 0x601050
            ++s.subState;
            return;
        case SetUpSub::WaitVoice:
            if (voiceClipPlaying()) {
                scriptState().part<IslandBlownBytes>().castStarted = 1;
                s.flags &= uint8_t(~kScriptUnskippable);
                s.enterState(CutsceneState::Steps);
            }
            return;
        default: return;
        }
    case CutsceneState::Steps: // exe 0x52b4f0
        if (s.steps->id == kScriptStepEnd) {
            s.enterState(CutsceneState::WrapUp);
            islandBlownWrapUp(s);
            scriptSequenceTick();
            return;
        }
        if (const RoutineRef routine = stateRoutine(s.steps->routine)) islandBlownStep(routine);
        if (scriptSkipRequested() != 0 && display().drawBuffer != 0) { // exe 0x511790
            scriptFade(kBlowUpFade, 0);
            s.enterState(CutsceneState::WrapUp);
        }
        scriptSequenceTick();
        return;
    case CutsceneState::WrapUp: islandBlownWrapUp(s); return;
    default: notPortedAt(kStoryPort, "script 0x3c state", s.state);
    }
}

bool islandBlownWhiteScreen() {
    ScriptState& s = scriptState();
    if (s.id != kIslandBlownScript) return false;
    const bool wrappingUp = s.cutsceneState() == CutsceneState::WrapUp;
    return !(scriptState().part<IslandBlownBytes>().whiteScreen == 0 ||
             (fadeState().busy != 0 && !(wrappingUp && s.subState >= kWhiteFromSub)));
}

} // namespace od::port
