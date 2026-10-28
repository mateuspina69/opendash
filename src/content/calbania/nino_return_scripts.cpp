// The scripts of the return to Nino island and Glyde's last attack there (ST17 / ST18, chapter 3):
//   0x39 (exe 0x529ac0, states 0x8a6e00: 0x529ae0 / 0x529d00 / 0x529d70; camera 0x8a6c1c, steps 0x8a6dc8, face tracks
//   0x8a6c04 / 0x8a6c14): ST17 sub 0 at story 9 (room handler 0x57f3f0 once flag 0x582 or 0x583 is set) -- the return
//   to Nino. State 0: the sequence, the replay pad, unskippable, the face words cleared, fade 0x12, the music faded
//   (0x222); once the music is down: the actors 0xffe swept, the warp to ST17 sub 2, DAT 0x8c (the next type-1 bank);
//   once loaded: the bank switched, fade 2, the player hidden at (-0x990, -0x801, 0xac0) yaw 0; once script driven
//   animation 0, the face tracks, sound 0x22; voice 5; state 1 once it plays. Steps: 0x51d380 (animation 0 once),
//   0x529ee0 (at 0x77 sub 1), 0x519440 (the phase cleared), 0x529f40 (at 0x5a unskippable, fade 0x12, the voice faded;
//   once idle the warp to ST17 sub 0 at (-0xadd, -0x801, -0x6d8), DAT 0x8d; once loaded the actors 0xffe swept + the
//   bank switched; voice 8; once it plays the player shown, fade 2, the next step), 0x52a0b0 (at 0 skippable again +
//   yaw 0x400; animation 0x84), 0x52a0e0 (animation 0x84 until 0x154, then face keys 1; 0x168 face 0; 0x1f4 face 2;
//   0x212 animation 0x85 + face 3; walks 0x140 along the yaw (while the animation's key is past 0xf) until the
//   animation ends, animation 0x86, then walks on; at 0x226 the music faded (0x111), fade 0x12, unskippable). The
//   runner 0x529d00 (skip: fade 0x22 + the music faded (0x222)). State 2: the voice faded; once idle and the music is
//   down; once no request: DAT 0x92; then request 2 -> ST17 sub 0 at (-0x97d, -0x800, -0x200) yaw 0x800 fade 2; once
//   switched the bank switched, the player back, sound 0x24, the block ended.
//   0x3a (exe 0x52a290, states 0x8a7068: 0x52a2b0 / 0x53a5e0 / 0x52a400; camera 0x8a6efc, steps 0x8a7038): ST18 at
//   story 9 / 0xa: Roll gets to the repair machine (sets up flag 0x2d for the attack). State 0: the actors 0xffe swept,
//   the sequence, the replay pad, unskippable, the face words cleared, the player at (0x13a, -1, 0x268) yaw 0x200, the
//   script's bytes cleared; once script driven animation 0x82 and the face tracks 0x8a6ee8 / 0x8a6ef0; voice 0xb; state
//   1 (the cast started) once it plays. Steps: 0x52a540 (animations 0x82 / 0x83 walking -0x48 along the yaw until the
//   animation ends, 0x84 at 0x127; at 0x1bb the warp to sub 1 at (0x60, -0xb00, 0xc0)), 0x52a680 (ST18's cells to
//   variant 1; at 0xc7 sub 5 at (0x20, -0xd60, 0x134); the player sinks 4 a tick down to -0xd60, animation 0x85),
//   0x52a830 (the player hidden; at 0xbd sub 3), 0x52a8a0 (at 0x3c / 0x4b / 0x5a a flying point from the pairs 0x8a7078
//   / 0x8a7090 + a camera shake; at 0x72 sub 1 at (0x20, -0xd60, 0x134)), 0x52aa50 (the player shown yaw 0xc00,
//   animation 0x85, face keys at 0xf / 0x20, fade 0x12 at 0xf9). State 2: the voice faded, DAT 0x96 (the next type-1
//   bank), then request 2 -> ST18 sub 1 at (0x60, -0xd81, 0x140) yaw 0xc00 fade 1; once switched the bank switched,
//   the player back with yaw 0xc00, the block ended.
//   0x3b (exe 0x52ab20, states 0x8a7488: 0x52ab40 / 0x53a5e0 / 0x52ad30; camera 0x8a7208, steps 0x8a7438): ST18 sub 1
//   at story 0xa (room handler 0x57f810 on flag 0x2e): the end of Glyde's last attack on Nino. State 0: the sequence,
//   the replay pad, unskippable, the face words cleared, fade 0x12; once covered and the player script driven: the
//   actors 0xffe swept, the warp to ST18 sub 5, DAT 0x9a (the next type-1 bank); once loaded: the bank switched, fade
//   0, the player at (-0x990, -0x801, 0xac0) yaw 0 hidden; animation 0, the face tracks 0x8a71ec / 0x8a71f8, voice 0xc;
//   state 1 once it plays. Steps: 0x52ae50 (animation 0; at 0x23d sub 3), 0x52aed0 (at 0xb3 sub 1 at (-0x20, -0xd60,
//   0x148) yaw 0xc00, the player shown), 0x52af40 (animation 0x86; at 6 face keys 2), 0x52afb0, 0x52afc0 (animations
//   0x87 / 0x88 at 0 / 0x145; at 0x1cb sub 3, the player hidden; from 0x28 the music faded and the screen ripples
//   0x52b180 at a shrinking then growing interval), 0x52b1c0 (0x59: sub 5), 0x52b220 (0x15d: sub 3), 0x52b280 (0x81:
//   sub 1 at (-0xb1, -0xd60, 0x148)), 0x52b2f0 (face keys, animation 0x89 until 0xfa, fade 0x12 at 0x15d). State 2:
//   the voice faded, then request 2 -> ST19 sub 0 at (0x300, -0x800, 0x680) fade 2 (0x12 unless skipped); once
//   switched: the map's route reset, flags 0x7c3 / 0x7bf cleared, the player back, the block ended.
#include "content/calbania/ch3_scripts.hpp"
#include "content/calbania/nino_return_scripts_state.hpp"
#include "content/calbania/ch3_rooms.hpp"

#include "engine/data_source.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/audio_logic.hpp"
#include "game/camera.hpp"
#include "game/display.hpp"
#include "game/fade_state.hpp"
#include "game/field_alias.hpp"
#include "game/port_gap.hpp"
#include "game/script_state.hpp"
#include "game/stage_files.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/table_records.hpp"
#include "game/transition.hpp"
#include "game/transition_state.hpp"
#include "game/visited_rooms.hpp"
#include "game/dat_names.hpp"

using namespace od::port::nino_return_scripts_state;

namespace od::port {

using namespace nino_return_scripts_state;

namespace {

constexpr uint32_t kVoiceFade = 8;
constexpr uint32_t kSweepAll = 0xffe, kSweepKeep = 0x80;
constexpr uint8_t kNinoStage = 0x17, kAttackStage = 0x18;
constexpr int16_t kSeaBedY = int16_t(0xf2a0);

// the in-stage warp to a sub-room of the stage (no fades), at a place or where the player is
void warpTo(uint8_t stage, uint8_t room, int16_t x = 0, int16_t y = 0, int16_t z = 0, int16_t yaw = 0) {
    requestTransition(kTransitionWarp, stage, room, x, y, z, yaw, kNoFade, kNoFade);
}
bool playerAnimEnded() { return (player().body.key.flags & kKeyJump) != 0; }
// a file still loads (the bank's DAT)
bool fileLoading() { return (fileLoads().loading & kFileLoading) != 0; }

// ==== script 0x39 ========================================================================================================
enum class ReturnSetUp : uint8_t { Start = 0, WaitMusic = 1, WaitBank = 2, TakeOver = 3, Voice = 4, WaitVoice = 5 };
enum class ReturnWrapUp : uint8_t { Fade = 0, WaitSound = 1, LoadBank = 2, Request = 3, End = 4 };
enum class ArrivalPhase : uint8_t { Wait = 0, Warp = 1, Load = 2, Voice = 3, Show = 4 };
enum class WalkAwayPhase : uint8_t { Look = 0, Mouth = 1, Eyes = 2, Turn = 3, Walk = 4, WalkOn = 5 };
constexpr RoutineId kStepStill = RoutineId::St24GateStep1, kStepToShore = RoutineId::NinoReturnScriptsStepToShore,
                    kStepClearPhase = RoutineId::MandaScriptABlockStep,
                    kStepArrive = RoutineId::NinoReturnScriptsStepArrive,
                   kStepFaceSea = RoutineId::NinoReturnScriptsStepFaceSea,
                   kStepWalkAway = RoutineId::NinoReturnScriptsStepWalkAway;
constexpr uint16_t kMusicFadeSlow = 0x222;

void ninoReturnStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    Player& p = player();
    switch (routine.id()) {
    case kStepStill:
        if (s.stepPhase != 0) return;
        playerAnim(0);
        ++s.stepPhase;
        return;
    case kStepToShore:
        if (s.stepTick == 0x77) warpTo(kNinoStage, 1);
        return;
    case kStepClearPhase: s.clearStepPhase(); return;
    case kStepArrive:
        switch (ArrivalPhase(s.stepPhase)) {
        case ArrivalPhase::Wait:
            if (s.stepTick != 0x5a) return;
            s.flags |= kScriptUnskippable;
            scriptFade(0x12, 0);
            voiceClipFadeOut(kVoiceFade); // exe 0x601220
            ++s.stepPhase;
            return;
        case ArrivalPhase::Warp:
            if (fadeState().busy != 0 || !voiceClipIdle()) return;
            warpTo(kNinoStage, 0, int16_t(0xf523), int16_t(0xf7ff), int16_t(0xf928));
            datLoadRequest(DatFile{"ST1702.DAT"}); // exe 0x40efe0
            ++s.stepPhase;
            return;
        case ArrivalPhase::Load:
            if (fileLoading()) return;
            actorsSweep(kSweepAll, kSweepKeep); // exe 0x5e05d0
            switchStageBank();                  // exe 0x5f3200
            ++s.stepPhase;
            return;
        case ArrivalPhase::Voice:
            voiceClipPlay(8); // exe 0x601050
            ++s.stepPhase;
            return;
        case ArrivalPhase::Show:
            if (!voiceClipPlaying()) return;
            p.setDrawn(true);
            scriptFade(2, 0);
            scriptNextStep(); // exe 0x5115c0(1)
            return;
        default: return;
        }
    case kStepFaceSea:
        if (s.stepTick == 0) {
            s.flags &= uint8_t(~kScriptUnskippable);
            p.rotation.y = 0x400;
        }
        playerAnim(0x84);
        return;
    case kStepWalkAway:
        switch (WalkAwayPhase(s.stepPhase)) {
        case WalkAwayPhase::Look:
            if (s.stepTick < 0x154) {
                playerAnim(0x84);
            } else {
                faceKeys(1);
                ++s.stepPhase;
            }
            break;
        case WalkAwayPhase::Mouth:
            if (s.stepTick == 0x168) {
                mouthKey(0);
                ++s.stepPhase;
            }
            break;
        case WalkAwayPhase::Eyes:
            if (s.stepTick == 0x1f4) {
                eyesKey(2);
                ++s.stepPhase;
            }
            break;
        case WalkAwayPhase::Turn:
            if (s.stepTick == 0x212) {
                playerAnim(0x85);
                eyesKey(3);
                ++s.stepPhase;
            }
            break;
        case WalkAwayPhase::Walk:
            if (int8_t(p.body.keyIndex) > 0xf) moveAlongYaw(p.pos, p.rotation.y, 0, 0, 0x140); // exe 0x5e36f0
            if (playerAnimEnded()) {
                playerAnim(0x86);
                ++s.stepPhase;
            }
            break;
        case WalkAwayPhase::WalkOn: moveAlongYaw(p.pos, p.rotation.y, 0, 0, 0x140); break;
        default: break;
        }
        if (s.stepTick == 0x226) {
            soundFadeOut(kMusicFades, 0x111, 0); // exe 0x5d5780
            scriptFade(0x12, 0);
            s.flags |= kScriptUnskippable;
        }
        return;
    default: notPortedAt(kStoryPort, "script 0x39 step", routine.pointerValue());
    }
}
// exe 0x529d00: as the generic runner, the skip also fading the music
void ninoReturnSteps(ScriptState& s) {
    if (s.steps->id != kScriptStepEnd) {
        if (const RoutineRef routine = stateRoutine(s.steps->routine)) ninoReturnStep(routine);
        if (scriptSkipRequested() == 0 || display().drawBuffer == 0) { // exe 0x511790
            scriptSequenceTick();
            faceTracksTick(s.faceTracks); // exe 0x5e3410
            return;
        }
        scriptFade(kScriptSkipWipe, 0);
        soundFadeOut(kMusicFades, kMusicFadeSlow, 0);
    }
    s.enterState(CutsceneState::WrapUp);
    scriptSequenceTick();
    faceTracksTick(s.faceTracks);
}

} // namespace

void ninoReturnScript() {
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x529ae0
        switch (ReturnSetUp(s.subState)) {
        case ReturnSetUp::Start:
            scriptSequenceStart(stateAddress(TableId::NinoReturnScriptsReturnCamera), scriptStepList<TableId::NinoReturnScriptsReturnSteps>); // exe 0x511390
            scriptReplayPadOn();                              // exe 0x5117f0
            s.flags |= kScriptUnskippable;
            clearPlayerAnglesAndFaces();
            scriptFade(0x12, 0);
            soundFadeOut(kMusicFades, kMusicFadeSlow, 0); // exe 0x5d5780
            s.subState = uint8_t(ReturnSetUp::WaitMusic);
            return;
        case ReturnSetUp::WaitMusic:
            if (fadeState().busy != 0 || !soundDone(kMusicFades)) return; // exe 0x5d5900
            actorsSweep(kSweepAll, kSweepKeep);                           // exe 0x5e05d0
            warpTo(kNinoStage, 2);
            datLoadRequest(DatFile{"ST1701.DAT"}); // exe 0x40efe0
            ++s.subState;
            return;
        case ReturnSetUp::WaitBank:
            if (fileLoading() || fadeState().busy != 0) return;
            switchStageBank(); // exe 0x5f3200
            scriptFade(2, 0);
            placePlayer(0xf6700000u, 0xf7ff0000u, 0x0ac00000u, 0);
            player().setDrawn(false);
            ++s.subState;
            return;
        case ReturnSetUp::TakeOver:
            if (playerToScriptIfFree() != 0) return; // exe 0x572300
            playerAnim(0);
            startFaceTracks(stateAddress(TableId::NinoReturnScriptsReturnEyes), stateAddress(TableId::NinoReturnScriptsReturnMouth));
            scriptSequenceTick(); // exe 0x5114d0
            ++s.subState;
            soundPlayFlatForced(0x22); // exe 0x5d5260
            return;
        case ReturnSetUp::Voice:
            voiceClipPlay(5); // exe 0x601050
            ++s.subState;
            return;
        case ReturnSetUp::WaitVoice:
            if (voiceClipPlaying()) {
                s.enterState(CutsceneState::Steps);
                s.flags &= uint8_t(~kScriptUnskippable);
            }
            return;
        default: return;
        }
    case CutsceneState::Steps: ninoReturnSteps(s); return;
    case CutsceneState::WrapUp: // exe 0x529d70
        switch (ReturnWrapUp(s.subState)) {
        case ReturnWrapUp::Fade:
            if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade); // exe 0x601220
            ++s.subState;
            [[fallthrough]];
        case ReturnWrapUp::WaitSound:
            if (voiceClipIdle() && soundDone(kMusicFades)) ++s.subState;
            return;
        case ReturnWrapUp::LoadBank:
            if (transitionRequest().state != 0 || fadeState().busy != 0) return;
            datLoadRequest(DatFile{"ST1705.DAT"}); // exe 0x40efe0
            ++s.subState;
            return;
        case ReturnWrapUp::Request:
            if (fileLoading()) return;
            requestTransition(kTransitionAccepted, kNinoStage, 0, int16_t(0xf683), int16_t(0xf800), int16_t(0xfe00), 0x800,
                              2, kNoFade);
            ++s.subState;
            return;
        case ReturnWrapUp::End:
            if (transitionRequest().state != 0 || fadeState().busy != 0) return;
            switchStageBank();    // exe 0x5f3200
            playerBackToPlay();   // exe 0x5723c0
            scriptReplayPadOff(); // exe 0x511890
            player().airborne = 0;
            player().setDrawn(true);
            soundPlayFlatForced(0x24); // exe 0x5d5260
            scriptEndBlock();          // exe 0x5116a0
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x39 state", s.state);
    }
}

namespace {

// ==== script 0x3a ========================================================================================================
constexpr int16_t kNoFlyFlag = -1;
constexpr uint8_t kFlyPointType = 0x1c;
constexpr int16_t kFlyPointTicks = 8;
// the flying points' pairs (exe data, 8 bytes apart): from, to
struct FlyPair : DataRecord {
    ShortPoint point;
    static FlyPair load(const TableReader& r) {
        FlyPair rec;
        rec.point = ShortPoint::load(r.sub(0x0, 6));
        return rec;
    }
};
const DataTable<RecordTable<FlyPair, 3, 8>> flyFrom{TableId::NinoReturnScriptsFlyFrom};
const DataTable<RecordTable<FlyPair, 3, 8>> flyTo{TableId::NinoReturnScriptsFlyTo};
constexpr int16_t kFlyOffsetXZ = 0x400, kFlyOffsetY = 0xf00;

enum class MachineSetUp : uint8_t { Start = 0, TakeOver = 1, Voice = 2, WaitVoice = 3 };
enum class MachineWrapUp : uint8_t { Fade = 0, Wait = 1, LoadBank = 2, Request = 3, End = 4 };
enum class RunPhase : uint8_t { Start = 0, Run = 1, Slow = 2, Stand = 3 };
enum class SinkPhase : uint8_t { Cells = 0, Sinking = 1 };
enum class LandPhase : uint8_t { Show = 0, Face = 1, Mouth = 2 };
constexpr RoutineId kStepRun = RoutineId::NinoReturnScriptsStepRun, kStepSink = RoutineId::NinoReturnScriptsStepSink,
                    kStepHide = RoutineId::NinoReturnScriptsStepHide,
                    kStepFlyPoints = RoutineId::NinoReturnScriptsStepFlyPoints,
                   kStepLand = RoutineId::NinoReturnScriptsStepLand;
constexpr int32_t kSeaBedFixed = int32_t(0xf2a00000u), kSinkPerTick = 0x40000;
constexpr uint8_t kSlowKeys = 0xa;

void rollToMachineStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    const int32_t t = s.stepTick;
    Player& p = player();
    switch (routine.id()) {
    case kStepRun:
        switch (RunPhase(s.stepPhase)) {
        case RunPhase::Start:
            playerAnim(0x82);
            ++s.stepPhase;
            [[fallthrough]];
        case RunPhase::Run:
            moveAlongYaw(p.pos, p.rotation.y, 0, 0, -0x48); // exe 0x5e36f0
            if (playerAnimEnded()) {
                playerAnim(0x83);
                ++s.stepPhase;
            }
            break;
        case RunPhase::Slow:
            if (int8_t(p.body.keyIndex) < kSlowKeys) moveAlongYaw(p.pos, p.rotation.y, 0, 0, -0x48);
            if (playerAnimEnded()) ++s.stepPhase;
            break;
        case RunPhase::Stand:
            if (t == 0x127) playerAnim(0x84);
            break;
        default: break;
        }
        if (s.stepTick == 0x1bb) {
            warpTo(kAttackStage, 1, 0x60, int16_t(0xf500), 0xc0, 0xc00);
            s.clearStepPhase();
        }
        return;
    case kStepSink:
        if (SinkPhase(s.stepPhase) == SinkPhase::Cells && transitionRequest().state == 0) {
            st18CellsToVariant(1); // exe 0x58d5f0 x 4
            s.stepPhase = uint8_t(SinkPhase::Sinking);
        }
        if (s.stepTick == 0xc7) warpTo(kAttackStage, 5, 0x20, kSeaBedY, 0x134, 0xc00);
        p.pos.y -= uint32_t(kSinkPerTick);
        if (p.pos.y < kSeaBedFixed) p.pos.y = kSeaBedFixed;
        playerAnim(0x85);
        return;
    case kStepHide:
        if (t == 0) p.setDrawn(false);
        else if (t == 0xbd) warpTo(kAttackStage, 3);
        return;
    case kStepFlyPoints:
        if (t == 0) {
            s.stepPhase = 0;
        } else if (t == 0x3c || t == 0x4b || t == 0x5a) {
            const uint32_t i = s.stepPhase;
            if (const uint32_t address = allocActorE0()) { // exe 0x5e1440
                ControllerActor& c = controllerActorAt(address);
                FlyPointFields& f = classState<FlyPointFields>(c);
                const FlyPair a = flyFrom[i], b = flyTo[i];
                c.flags = kActorLive;
                c.type = kFlyPointType;
                f.from.x = int16_t(a.point.x + kFlyOffsetXZ);
                f.from.y = int16_t(a.point.y - kFlyOffsetY);
                f.from.z = int16_t(a.point.z + kFlyOffsetXZ);
                s.stepPhase = uint8_t(i + 1u);
                f.to.x = int16_t(b.point.x + kFlyOffsetXZ);
                f.to.y = int16_t(b.point.y - kFlyOffsetY);
                f.to.z = int16_t(b.point.z + kFlyOffsetXZ);
                f.ticks = kFlyPointTicks;
                f.flag = kNoFlyFlag;
            }
            cameraShake(true, 0x300, 0x40); // exe 0x40ea80
        } else if (t == 0x72) {
            warpTo(kAttackStage, 1, 0x20, kSeaBedY, 0x134, 0xc00);
            s.clearStepPhase();
        }
        return;
    case kStepLand:
        switch (LandPhase(s.stepPhase)) {
        case LandPhase::Show:
            if (transitionRequest().state == 0) {
                p.setDrawn(true);
                p.rotation.y = 0xc00;
                playerAnim(0x85);
                ++s.stepPhase;
            }
            break;
        case LandPhase::Face:
            playerAnim(0x85);
            if (s.stepTick >= 0xf) {
                faceKeys(1);
                ++s.stepPhase;
            }
            break;
        case LandPhase::Mouth:
            if (s.stepTick == 0x20) {
                mouthKey(2);
                ++s.stepPhase;
            }
            break;
        default: break;
        }
        if (s.stepTick == 0xf9) scriptFade(0x12, 0);
        return;
    default: notPortedAt(kStoryPort, "script 0x3a step", routine.pointerValue());
    }
}

} // namespace

void rollToMachineScript() {
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x52a2b0
        switch (MachineSetUp(s.subState)) {
        case MachineSetUp::Start:
            actorsSweep(kSweepAll, kSweepKeep);                 // exe 0x5e05d0
            scriptSequenceStart(stateAddress(TableId::NinoReturnScriptsMachineCamera), scriptStepList<TableId::NinoReturnScriptsMachineSteps>); // exe 0x511390
            scriptReplayPadOn();                                // exe 0x5117f0
            s.flags |= kScriptUnskippable;
            clearPlayerAnglesAndFaces();
            placePlayer(0x013a0000u, 0xffff0000u, 0x02680000u, 0x200);
            s.clearScriptByteRun();
            scriptSequenceTick(); // exe 0x5114d0
            ++s.subState;
            [[fallthrough]];
        case MachineSetUp::TakeOver:
            if (playerToScriptIfFree() != 0) return; // exe 0x572300
            playerAnim(0x82);
            startFaceTracks(stateAddress(TableId::NinoReturnScriptsMachineEyes), stateAddress(TableId::NinoReturnScriptsMachineMouth));
            ++s.subState;
            return;
        case MachineSetUp::Voice:
            playerAnim(0x82);
            voiceClipPlay(0xb); // exe 0x601050
            ++s.subState;
            return;
        case MachineSetUp::WaitVoice:
            playerAnim(0x82);
            if (voiceClipPlaying()) {
                scriptState().part<RollToMachineBytes>().castStarted = 1;
                s.enterState(CutsceneState::Steps);
                s.flags &= uint8_t(~kScriptUnskippable);
            }
            return;
        default: return;
        }
    case CutsceneState::Steps: scriptStepsState(rollToMachineStep); return; // exe 0x53a5e0
    case CutsceneState::WrapUp:                                              // exe 0x52a400
        switch (MachineWrapUp(s.subState)) {
        case MachineWrapUp::Fade:
            if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade); // exe 0x601220
            ++s.subState;
            [[fallthrough]];
        case MachineWrapUp::Wait: ++s.subState; return;
        case MachineWrapUp::LoadBank:
            if (fadeState().busy != 0) return;
            datLoadRequest(DatFile{"ST1800.DAT"}); // exe 0x40efe0
            ++s.subState;
            return;
        case MachineWrapUp::Request:
            if (fileLoading()) return;
            requestTransition(kTransitionAccepted, kAttackStage, 1, 0x60, int16_t(0xf27f), 0x140, 0xc00, 1, kNoFade);
            ++s.subState;
            return;
        case MachineWrapUp::End: {
            if (transitionRequest().state != 0 || fadeState().busy != 0) return;
            switchStageBank(); // exe 0x5f3200
            Player& p = player();
            p.rotation.y = 0xc00;
            p.setDrawn(true);
            playerBackToPlay();   // exe 0x5723c0
            scriptReplayPadOff(); // exe 0x511890
            p.airborne = 0;
            scriptEndBlock(); // exe 0x5116a0
            return;
        }
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x3a state", s.state);
    }
}

namespace {

// ==== script 0x3b ========================================================================================================
// a screen ripple (pool a0 type 0x16 variant 0, content/nino/glyde_attack): its centre on the screen, its start radius
// (<< 8), its growth a tick and its largest radius
struct RippleFields {
    explicit RippleFields(const EffectActor& actor) : RippleFields(const_cast<EffectActor&>(actor)) {}
    explicit RippleFields(EffectActor& actor) : startRadius(actor.params[0]), growth(actor.params[1]), centreX(actor.pos.x), centreY(actor.pos.y), maxRadius(actor.posSpare) {}
    FieldAlias<int8_t, uint8_t> startRadius;
    FieldAlias<int8_t, uint8_t> growth;
    int16_t& centreX;
    int16_t& centreY;
    int16_t& maxRadius;
};
constexpr uint8_t kRippleType = 0x16;
constexpr int16_t kRippleX = 0x6b, kRippleY = 0x44, kRippleMax = 0x100;
constexpr int8_t kRippleGrowth = 6;
constexpr uint8_t kRippleByte2 = 2; // its parameter byte 2 (its reader is not traced)
constexpr int16_t kRippleStart = 0x1e, kRippleStep = 6, kRippleFastest = 4, kRippleSlowest = 0x12;
constexpr int8_t kRippleFirstSize = 4;

enum class EndSetUp : uint8_t { Start = 0, TakeOver = 1, WaitBank = 2, Place = 3, Voice = 4, WaitVoice = 5 };
enum class EndWrapUp : uint8_t { Fade = 0, WaitVoice = 1, Request = 2, End = 3 };
enum class RipplePhase : uint8_t { Start = 0, WaitMusic = 1, Faster = 2, Steady = 3, Slower = 4 };
constexpr RoutineId kStepWatch = RoutineId::NinoReturnScriptsStepWatch,
                    kStepSurface = RoutineId::NinoReturnScriptsStepSurface,
                    kStepLook = RoutineId::NinoReturnScriptsStepLook,
                    kStepClear = RoutineId::NinoReturnScriptsStepClear,
                   kStepRipples = RoutineId::NinoReturnScriptsStepRipples,
                   kStepDive = RoutineId::NinoReturnScriptsStepDive, kStepRise = RoutineId::NinoReturnScriptsStepRise,
                   kStepBack = RoutineId::NinoReturnScriptsStepBack,
                   kStepLast = RoutineId::NinoReturnScriptsStepLast;
constexpr uint32_t kFlagAttackShore = 0x7c3, kFlagAttackSea = 0x7bf;

// exe 0x52b180(size): a screen ripple of the size
void screenRipple(int8_t size) {
    const uint32_t address = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (address == 0) return;
    EffectActor& e = effectActorAt(address);
    RippleFields r(e);
    e.flags = kActorLive;
    e.type = kRippleType;
    e.variant = 0;
    r.centreX = kRippleX;
    r.centreY = kRippleY;
    r.maxRadius = kRippleMax;
    r.startRadius = size;
    r.growth = kRippleGrowth;
    e.params[2] = kRippleByte2;
}
// one tick of the ripples' countdown; true when it ran out (the ripple shown)
bool rippleDue(AttackEndBytes& bytes) {
    bytes.rippleCountdown -= 1u;
    if (bytes.rippleCountdown != 0) return false;
    screenRipple(bytes.rippleSize);
    return true;
}
void attackEndStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    const int32_t t = s.stepTick;
    Player& p = player();
    switch (routine.id()) {
    case kStepWatch:
        if (s.stepPhase == 0) {
            playerAnim(0);
            ++s.stepPhase;
        }
        if (t == 0x23d) warpTo(kAttackStage, 3);
        return;
    case kStepSurface:
        if (t == 0xb3) {
            warpTo(kAttackStage, 1, -0x20, kSeaBedY, 0x148, 0xc00);
            p.setDrawn(true);
        }
        return;
    case kStepLook:
        if (t < 0) return;
        if (t <= 5) {
            playerAnim(0x86);
        } else if (t == 6) {
            p.setDrawn(true);
            playerAnim(0x86);
            faceKeys(2);
        }
        return;
    case kStepClear: s.stepPhase = 0; return;
    case kStepRipples: {
        if (t == 0) {
            playerAnim(0x87);
        } else if (t == 0x145) {
            playerAnim(0x88);
        } else if (t == 0x1cb) {
            warpTo(kAttackStage, 3);
            p.setDrawn(false);
        }
        AttackEndBytes& bytes = scriptState().part<AttackEndBytes>();
        switch (RipplePhase(s.stepPhase)) {
        case RipplePhase::Start:
            bytes.rippleInterval = kRippleStart;
            bytes.rippleSize = kRippleFirstSize;
            ++s.stepPhase;
            [[fallthrough]];
        case RipplePhase::WaitMusic:
            if (s.stepTick == 0x28) {
                bytes.rippleCountdown = 1;
                ++s.stepPhase;
                soundFadeOut(kMusicFades, 0x333, 0); // exe 0x5d5780
            }
            return;
        case RipplePhase::Faster:
            if (!rippleDue(bytes)) return;
            bytes.rippleInterval -= uint32_t(kRippleStep);
            ++bytes.rippleSize;
            bytes.rippleCountdown = uint16_t(bytes.rippleInterval);
            if (bytes.rippleInterval < kRippleFastest) {
                bytes.rippleInterval = kRippleFastest;
                bytes.rippleCountdown = uint16_t(kRippleFastest);
                ++s.stepPhase;
            }
            return;
        case RipplePhase::Steady:
            if (rippleDue(bytes)) bytes.rippleCountdown = uint16_t(bytes.rippleInterval);
            if (s.stepTick == 0xa5) ++s.stepPhase;
            return;
        case RipplePhase::Slower:
            if (!rippleDue(bytes)) return;
            --bytes.rippleSize;
            if (bytes.rippleSize < kRippleFirstSize) bytes.rippleSize = kRippleFirstSize;
            bytes.rippleInterval += uint32_t(kRippleStep);
            bytes.rippleCountdown = uint16_t(bytes.rippleInterval);
            if (bytes.rippleInterval > kRippleSlowest) ++s.stepPhase;
            return;
        default: return;
        }
    }
    case kStepDive:
        if (t == 0x59) warpTo(kAttackStage, 5);
        return;
    case kStepRise:
        if (t == 0x15d) warpTo(kAttackStage, 3);
        return;
    case kStepBack:
        if (t == 0x81) warpTo(kAttackStage, 1, int16_t(0xff4f), kSeaBedY, 0x148, 0xc00);
        return;
    case kStepLast:
        if (t == 0) {
            p.setDrawn(true);
            p.rotation.y = 0xc00;
            faceKeys(1);
        } else if (t == 0xfa || t == 0x118) {
            mouthKey(3);
        } else if (t == 0xfb || t == 0x12c) {
            mouthKey(1);
        } else if (t == 0x15d) {
            scriptFade(0x12, 0);
        }
        if (s.stepTick < 0xfa) playerAnim(0x89);
        return;
    default: notPortedAt(kStoryPort, "script 0x3b step", routine.pointerValue());
    }
}

} // namespace

void ninoAttackEndScript() {
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x52ab40
        switch (EndSetUp(s.subState)) {
        case EndSetUp::Start:
            scriptSequenceStart(stateAddress(TableId::NinoReturnScriptsEndCamera), scriptStepList<TableId::NinoReturnScriptsEndSteps>); // exe 0x511390
            scriptReplayPadOn();                        // exe 0x5117f0
            s.flags |= kScriptUnskippable;
            clearPlayerAnglesAndFaces();
            scriptFade(0x12, 0);
            s.subState = uint8_t(EndSetUp::TakeOver);
            [[fallthrough]];
        case EndSetUp::TakeOver:
            if (fadeState().busy != 0 || playerToScriptIfFree() != 0) return; // exe 0x572300
            actorsSweep(kSweepAll, kSweepKeep);                               // exe 0x5e05d0
            warpTo(kAttackStage, 5);
            datLoadRequest(DatFile{"ST1803.DAT"}); // exe 0x40efe0
            ++s.subState;
            return;
        case EndSetUp::WaitBank:
            if (fileLoading() || fadeState().busy != 0) return;
            switchStageBank(); // exe 0x5f3200
            scriptFade(0, 0);
            placePlayer(0xf6700000u, 0xf7ff0000u, 0x0ac00000u, 0);
            player().setDrawn(false);
            ++s.subState;
            return;
        case EndSetUp::Place:
            playerAnim(0);
            startFaceTracks(stateAddress(TableId::NinoReturnScriptsEndEyes), stateAddress(TableId::NinoReturnScriptsEndMouth));
            scriptSequenceTick(); // exe 0x5114d0
            ++s.subState;
            return;
        case EndSetUp::Voice:
            voiceClipPlay(0xc); // exe 0x601050
            ++s.subState;
            return;
        case EndSetUp::WaitVoice:
            if (voiceClipPlaying()) {
                s.enterState(CutsceneState::Steps);
                s.flags &= uint8_t(~kScriptUnskippable);
            }
            return;
        default: return;
        }
    case CutsceneState::Steps: scriptStepsState(attackEndStep); return; // exe 0x53a5e0
    case CutsceneState::WrapUp:                                          // exe 0x52ad30
        switch (EndWrapUp(s.subState)) {
        case EndWrapUp::Fade:
            if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade); // exe 0x601220
            ++s.subState;
            [[fallthrough]];
        case EndWrapUp::WaitVoice:
            if (voiceClipIdle()) ++s.subState;
            return;
        case EndWrapUp::Request:
            requestTransition(kTransitionAccepted, 0x19, 0, 0x300, int16_t(0xf800), 0x680, 0, 2,
                              (s.flags & kScriptSkipTaken) ? kNoFade : 0x12);
            ++s.subState;
            return;
        case EndWrapUp::End:
            if (transitionRequest().state != 0 || fadeState().busy != 0) return;
            mapRouteReset(); // exe 0x55c560
            clearStoryFlag(kFlagAttackShore);
            clearStoryFlag(kFlagAttackSea);
            playerBackToPlay();   // exe 0x5723c0
            scriptReplayPadOff(); // exe 0x511890
            player().airborne = 0;
            player().setDrawn(true);
            scriptEndBlock(); // exe 0x5116a0
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x3b state", s.state);
    }
}

} // namespace od::port
