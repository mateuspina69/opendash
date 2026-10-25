// The ice ruin's story scripts (see ice_ruin_scripts.hpp), one routine per exe routine. Names are hypotheses from the
// observed behaviour. Render output (the shadow list exe 0x5f20f0, the OT sprites of the menus) is left out.
//
// Scripts (id, handler, stage, trigger):
//   0x48 (0x52f8f0)  ST2D sub 5 (room handler 0x5846c0, flags 0x72 / 0x73): the shrinking block 0x4dd770 (variant 1)
//                    shown in sub 7 / 9 (an in-stage warp there and back to sub 5); encounter group 6
//   0x49 (0x52fb60)  ST2F sub 6, the first visit: the cast 0x8a9518 / 0x8a952c / 0x8a9540 (pool 0x20 type 0x4a sub 1)
//                    cued by the timeline; the end reloads ST2F sub 6
//   0x4a (0x52fe00)  ST2F sub 7 (flag 0x75 clear): the 0x4d6b40 tutorial (record 0x8a9600), DAT 0xe5, sound 0x35;
//                    the end puts the 0x8a9614 actor in its slot
//   0x4b (0x530010)  ST2F sub 7 (flag 0x75 set): the 0x4d6b40 (record 0x8a96d0) scene; the request to ST0B sub 1
#include "content/yosyonke/ice_ruin_scripts.hpp"
#include "content/yosyonke/ice_ruin_scripts_state.hpp"

#include "engine/routine_ids.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/audio_logic.hpp"
#include "game/display.hpp"
#include "game/encounter.hpp"
#include "game/fade_state.hpp"
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
#include "game/dat_names.hpp"

using namespace od::port::ice_ruin_scripts_state;

namespace od::port {

namespace {


// the scenes' actor sweeps: every actor class but the first effects group / the second groups
constexpr uint32_t kSweepSceneActors = kSweepActors & ~kSweepEffectsGroup0;
constexpr uint32_t kSweepSecondGroups =
    kSweepAnimatedGroup2 | kSweepProjectiles | kSweepStaticsGroup2 | kSweepEffectsGroup2;
constexpr uint8_t kSkipFade = 0x22;
constexpr uint32_t kScenesSoundMask = 1;
constexpr int16_t kScenesSoundFade = 0x666;
constexpr uint32_t kTutorialSoundsMask = 0xf;
constexpr int8_t kTutorialWindow = 4;

// the empty step routine of the sequences
constexpr RoutineId kNoStepRoutine = RoutineId::EmptyRoutine;

ScriptState& block() { return scriptState(); }

// the player placed at a scene's start (the inlined copies of the set-ups): the move direction's angles and the face
// cleared, the position (16.16) and the yaw
void playerAt(int32_t x, int32_t y, int32_t z, int16_t yaw) {
    Player& p = player();
    p.clearDirectionOffsetWords();
    p.idleFaces[0] = 0;
    p.idleFaces[1] = 0;
    p.pos.x = x;
    p.pos.y = y;
    p.pos.z = z;
    p.rotation.y = yaw;
}
constexpr int32_t kTutorialX = 0, kTutorialY = int32_t(0xfe000000u), kTutorialZ = int32_t(0xf8000000u);
constexpr int16_t kTutorialYaw = 0x800;
// the end place of scripts 0x4a (exe 0x52fe90; as script 0x5a's): on the ground at the tutorial's place
void playerBackAtTutorial() {
    Player& p = player();
    p.airborne = 0;
    p.pos.x = kTutorialX;
    p.pos.y = kTutorialY;
    p.pos.z = kTutorialZ;
    p.rotation.y = kTutorialYaw;
}

// the step sequence's runners: the current step's routine (0: none) run each tick until the list ends
template <class F> void runStep(F step) {
    if (const RoutineRef routine = stateRoutine(block().steps->routine)) step(routine);
}
bool stepsEnded() { return block().steps->id == kScriptStepEnd; }
bool skipTaken() { return scriptSkipRequested() != 0 && display().drawBuffer != 0; } // exe 0x511790
// exe 0x52fa20 (script 0x48): a skip fades and ends the steps; the list's end ends them (no fade)
template <class F> void runStepsEndAsWord(F step) {
    ScriptState& s = block();
    if (!stepsEnded()) {
        runStep(step);
        if (!skipTaken()) {
            scriptSequenceTick();
            return;
        }
        scriptFade(kSkipFade, 0);
    }
    s.enterState(CutsceneState::WrapUp);
    scriptSequenceTick();
}
// exe 0x5358e0: a skip -> the fade, the wrap-up at its first sub-state; the list's end -> the wrap-up at sub-state 1
// (no fade)
template <class F> void runStepsToWrapUp(F step) {
    ScriptState& s = block();
    if (!stepsEnded()) {
        runStep(step);
        if (skipTaken()) {
            scriptFade(kSkipFade, 0);
            s.state = uint8_t(CutsceneState::WrapUp);
            s.subState = 0;
        }
    } else {
        s.state = uint8_t(CutsceneState::WrapUp);
        s.subState = 1;
    }
    scriptSequenceTick();
}
// exe 0x530140: as 0x5358e0, but the list's end fades too (only while the draw buffer is the second)
template <class F> void runStepsFadingAtEnd(F step) {
    ScriptState& s = block();
    if (!stepsEnded()) {
        runStep(step);
        if (skipTaken()) {
            scriptFade(kSkipFade, 0);
            s.state = uint8_t(CutsceneState::WrapUp);
            s.subState = 0;
        }
    } else if (display().drawBuffer != 0) {
        scriptFade(kSkipFade, 0);
        s.state = uint8_t(CutsceneState::WrapUp);
        s.subState = 1;
    }
    scriptSequenceTick();
}

// ==== script 0x48 (states 0x52f910 / 0x52fa20 / 0x52fa70; cameras 0x8a9488 / 0x8a94b8 (op 0x40: records 0x8a9460 /
// 0x8a9474, the block 0x4dd770 variant 1), steps 0x8a94e8 / 0x8a94f8 (one empty step, 0x5a ticks)) ===============
// State 0: the pad replay; once scripted the actors swept, the warp to ST2D sub 7 at (0, -0x200, 0x310) (flag 0x73
// clear) or sub 9 at (0, 0, 0x310) yaw 0x800, the sequence skipped by one and the camera of that room; then the steps
// with the player hidden. End: the warp back to ST2D sub 5 at (+-0x80, 0, 0x690) yaw 0x800; then (a skip fades 0) the
// actors swept, the script slot's encounter group 6, the player back, the block ended.
constexpr uint32_t kFlagShrinkSecond = 0x73; // the second showing (sub 9)
constexpr uint8_t kSt2dStage = 0x2d, kShrinkRoomFirst = 7, kShrinkRoomSecond = 9, kShrinkBackRoom = 5;
constexpr int16_t kShrinkWarpZ = 0x310, kShrinkFirstY = int16_t(0xfe00), kShrinkBackZ = 0x690;
constexpr int kShrinkGroup = 6;

void shrinkSetUp() { // exe 0x52f910
    ScriptState& s = block();
    switch (s.subState) {
    case 0:
        scriptReplayPadOn();
        s.subState = 1;
        [[fallthrough]];
    case 1:
        if (playerToScriptIfFree() != 0) return;
        s.subState = 2;
        actorsSweep(kSweepSceneActors, kSweepFree);
        if (!storyFlag(kFlagShrinkSecond)) {
            requestTransition(kTransitionWarp, kSt2dStage, kShrinkRoomFirst, 0, kShrinkFirstY, kShrinkWarpZ, 0x800,
                              kNoFade, kNoFade);
            scriptSequenceSkip(1);
            scriptSequenceStart(stateAddress(TableId::IceRuinScriptsShrinkFirstCameraScript), stateAddress(TableId::IceRuinScriptsShrinkFirstSteps));
        } else {
            requestTransition(kTransitionWarp, kSt2dStage, kShrinkRoomSecond, 0, 0, kShrinkWarpZ, 0x800, kNoFade,
                              kNoFade);
            scriptSequenceSkip(1);
            scriptSequenceStart(stateAddress(TableId::IceRuinScriptsShrinkSecondCameraScript), stateAddress(TableId::IceRuinScriptsShrinkSecondSteps));
        }
        scriptSequenceTick();
        return;
    case 2:
        s.enterStateKeepingPhases(uint8_t(CutsceneState::Steps));
        player().setDrawn(false);
        scriptSequenceTick();
        return;
    default: return;
    }
}
void shrinkWrapUp() { // exe 0x52fa70
    ScriptState& s = block();
    switch (s.subState) {
    case 0: s.subState = 1; return;
    case 1:
        if (!fadeState().idle()) return;
        s.subState = 2;
        return;
    case 2:
        s.subState = 3;
        requestTransition(kTransitionWarp, kSt2dStage, kShrinkBackRoom, storyFlag(kFlagShrinkSecond) ? 0x80 : -0x80, 0,
                          kShrinkBackZ, 0x800, kNoFade, kNoFade);
        return;
    case 3:
        if (s.flags & kScriptSkipTaken) scriptFade(0, 0);
        actorsSweep(kSweepSceneActors, kSweepFree);
        encounterSpawnGroupScriptSlot(kShrinkGroup); // exe 0x5e66b0
        playerBackToPlay();                          // exe 0x5723c0
        scriptReplayPadOff();
        player().flags |= kActorDrawn;
        scriptEndBlock();
        return;
    default: return;
    }
}
void shrinkScript() {
    ScriptState& s = block();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: shrinkSetUp(); return;
    case CutsceneState::Steps:
        runStepsEndAsWord([](const RoutineRef& routine) {
            if (!routine.is(kNoStepRoutine)) notPortedAt(kStoryPort, "script 0x48 step", routine.pointerValue());
        });
        return;
    case CutsceneState::WrapUp: shrinkWrapUp(); return;
    default: notPortedAt(kStoryPort, "script 0x48 state", s.state);
    }
}

// ==== script 0x49 (states [0x8a95f0]: 0x52fb80 / 0x52fa20 / 0x52fbe0; camera 0x8a9554, steps 0x8a95c8) ============
// The cast into the script's actors 0..2, the player put at (-0x1a40, -0x100, 0x1766) yaw 0xc00 and walked forward by
// the replay pad, the cast's cues at ticks 0 / 0xa / 0x1e / 0x21 / 0x23; the end frees the classes and reloads ST2F
// sub 6 at (-0x192a, -0x100, 0x1766) yaw 0x800 (an in-stage warp).
constexpr TableId kCritterRecords[] = {TableId::IceRuinScriptsCritterRecord0, TableId::IceRuinScriptsCritterRecord1, TableId::IceRuinScriptsCritterRecord2};
constexpr int32_t kSceneStartX = int32_t(0xe5c00000u), kSceneStartY = int32_t(0xff000000u), kSceneStartZ = int32_t(0x17660000u);
constexpr int16_t kSceneStartYaw = 0xc00;
constexpr int32_t kSceneWalkEnd = 0x1e;
constexpr uint8_t kSt2fStage = 0x2f, kCritterSceneRoom = 6;
constexpr uint8_t kCueStand = 1, kCueLeap = 2;
// the empty step, the player's walk, the cast's cues
constexpr RoutineId kWalkStep = RoutineId::IceRuinScriptsWalkStep, kCueStep = RoutineId::IceRuinScriptsCueStep;

// the cue of the script's actor k (its first parameter)
void cue(uint32_t k, uint8_t value) { (*block().actors[k]).params[0] = value; }

void critterSceneStep(const RoutineRef& routine) {
    ScriptState& s = block();
    const int32_t t = s.stepTick;
    switch (routine.id()) {
    case kNoStepRoutine: return;
    case kWalkStep: { // the player placed, then walked forward until the walk's end (its point kept)
        Player& p = player();
        if (t == 0) {
            p.clearDirectionOffsetWords();
            p.idleFaces[0] = 0;
            p.idleFaces[1] = 0;
            p.pos.x = kSceneStartX;
            p.pos.y = kSceneStartY;
            p.pos.z = kSceneStartZ;
            p.rotation.y = kSceneStartYaw;
            p.velocity.x = 0;
            p.velocity.y = 0;
            p.velocity.z = 0;
            p.airborne = 0;
            p.wasAirborne = 0;
        } else if (t == kSceneWalkEnd) {
            ++s.stepPhase;
            CritterSceneBytes& bytes = scriptState().part<CritterSceneBytes>();
            bytes.walkEndX = p.pos.x.whole;
            bytes.walkEndY = p.pos.y.whole;
            bytes.walkEndZ = p.pos.z.whole;
            replayPad().held = 0;
            replayPad().pressed = 0;
        }
        if (s.stepPhase == 0)
            replayPad().held = uint16_t(p.button(PlayerButton::Forward) | p.button(PlayerButton::Walk));
        return;
    }
    case kCueStep:
        switch (t) {
        case 0: cue(0, kCueStand); return;
        case 0xa: cue(1, kCueStand); return;
        case 0x1e: cue(0, kCueLeap); return;
        case 0x21: cue(1, kCueLeap); return;
        case 0x23: cue(2, kCueLeap); return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x49 step", routine.pointerValue());
    }
}
void critterScene() {
    ScriptState& s = block();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x52fb80
        switch (s.subState) {
        case 0:
            scriptReplayPadOn(); // exe 0x5117f0
            s.subState = 1;
            [[fallthrough]];
        case 1:
            scriptSequenceStart(stateAddress(TableId::IceRuinScriptsCritterSceneCamera), stateAddress(TableId::IceRuinScriptsCritterSceneSteps)); // exe 0x511390
            scriptSequenceTick();                                         // exe 0x5114d0
            s.enterStateKeepingPhases(uint8_t(CutsceneState::Steps));
            for (const TableId record : kCritterRecords) scriptActorSpawnRecord(stateAddress(record)); // exe 0x511740
            return;
        default: return;
        }
    case CutsceneState::Steps: // exe 0x52fa20: the step; a skip fades 0x22 and wraps up; the list's end wraps up
        if (stepsEnded()) {
            s.enterState(CutsceneState::WrapUp);
        } else {
            critterSceneStep(stateRoutine(s.steps->routine));
            if (skipTaken()) {
                scriptFade(kSkipFade, 0);
                s.enterState(CutsceneState::WrapUp);
            }
        }
        scriptSequenceTick();
        return;
    case CutsceneState::WrapUp: // exe 0x52fbe0
        switch (s.subState) {
        case 0: s.subState = 1; return;
        case 1:
            if (fadeState().idle()) s.subState = 2;
            return;
        case 2:
            if (s.flags & kScriptSkipTaken) scriptFade(1, 0);
            actorsSweep(kSweepSceneActors, kSweepFree);
            playerBackToPlay();   // exe 0x5723c0
            scriptReplayPadOff(); // exe 0x511890
            requestTransition(kTransitionWarp, kSt2fStage, kCritterSceneRoom, -0x192a, -0x100, 0x1766, 0x800, kNoFade, kNoFade);
            scriptEndBlock(); // exe 0x5116a0
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x49 state", s.state);
    }
}

// ==== script 0x4a (states 0x52fe20 / 0x5358e0 / 0x52fe90; camera 0x8a9628, steps 0x8a96b0 = 0x52ff30 (0x1c2 ticks))
// The camera, the pad replay, once scripted the sound fade, animation 0. Step 0x52ff30: the player at the tutorial's
// place, the script actor of record 0x8a9600 (0x4d6b40 variant 2); once the sounds and the voice are done DAT 0xe5;
// once loaded (draw buffer 1) sound 0x35 and the cutscene skippable again. End: a skip waits for the sounds then fades
// 1; the second groups swept, record 0x8a9614 (0x4d6b40 variant 1) into its slot, the player back, the block ended.
constexpr RoutineId kTutorialStep = RoutineId::IceRuinScriptsTutorialStep;
constexpr uint16_t kTutorialSound = 0x35;
constexpr DatFile kTutorialDat{"ST2F01.DAT"};

void tutorialStep(const RoutineRef& routine) {
    ScriptState& s = block();
    switch (routine.id()) {
    case kTutorialStep:
        switch (s.stepPhase) {
        case 0:
            playerAt(kTutorialX, kTutorialY, kTutorialZ, kTutorialYaw);
            scriptActorSpawnRecord(stateAddress(TableId::IceRuinScriptsTutorialCastRecord)); // exe 0x511740
            ++s.stepPhase;
            return;
        case 1:
            if (soundDone(kTutorialSoundsMask) && voiceClipIdle()) { // exe 0x5d5900 / 0x601260
                datLoadRequest(kTutorialDat);                         // exe 0x40efe0
                ++s.stepPhase;
            }
            return;
        case 2:
            if (!(fileLoads().loading & kFileLoading) && display().drawBuffer == 1) {
                soundPlayFlatForced(kTutorialSound); // exe 0x5d5260
                s.flags &= uint8_t(~kScriptUnskippable);
                ++s.stepPhase;
            }
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x4a step", routine.pointerValue());
    }
}
void tutorialScript() {
    ScriptState& s = block();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x52fe20
        if (s.subState == 0) {
            scriptSequenceStart(stateAddress(TableId::IceRuinScriptsTutorialCamera), stateAddress(TableId::IceRuinScriptsTutorialSteps));
            scriptReplayPadOn();
            s.subState = 1;
            s.flags |= kScriptUnskippable;
        } else if (s.subState != 1) {
            return;
        }
        if (playerToScriptIfFree() != 0) return;
        soundFadeOut(kScenesSoundMask, kScenesSoundFade, 0);
        s.enterState(CutsceneState::Steps);
        playerScriptAnim(PlayerAnim::Idle, 0, true);
        return;
    case CutsceneState::Steps: runStepsToWrapUp(&tutorialStep); return; // exe 0x5358e0
    case CutsceneState::WrapUp:                                         // exe 0x52fe90
        if (s.subState == 0) {
            if (!fadeState().idle()) return;
            if (s.flags & kScriptSkipTaken) {
                if (!soundDone(kTutorialSoundsMask)) return;
                scriptFade(1, 0);
            }
            ++s.subState;
            return;
        }
        if (s.subState != 1) return;
        actorsSweep(kSweepSecondGroups, kSweepFree);
        spawnRecords(stateAddress(TableId::IceRuinScriptsTutorialEndRecord), 1);
        playerBackToPlay(); // exe 0x5723c0
        scriptReplayPadOff();
        playerBackAtTutorial();
        scriptEndBlock();
        return;
    default: notPortedAt(kStoryPort, "script 0x4a state", s.state);
    }
}

// ==== script 0x4b (states 0x530030 / 0x530140 / 0x5301b0; camera 0x8a96e4 (op 0x40: record 0x8a96d0, the cast
// 0x4d6b40 variant 3), steps 0x8a9740 = 0x530280 (0x1f4 ticks)) ==================================================
// Fade 0x12, the pad replay; once dark the actors swept, window 4 closed, the camera, the player at the tutorial's
// place, scripted, the sound fade. Step 0x530280: fade 2, then the cutscene skippable again. End (sub-state 1 at the
// list's end): the request to ST0B sub 1 at (0xc0, -0x20, 0x180) yaw 0x800 (fades 2 / none), the second groups swept;
// once taken the player back (shown), the block ended.
constexpr RoutineId kTutorialSceneStep = RoutineId::IceRuinScriptsTutorialSceneStep;
constexpr uint8_t kSceneFadeOut = 0x12, kSceneFadeIn = 2;
constexpr uint8_t kSt0bStage = 0xb, kSt0bRoom = 1;

void tutorialSceneStep(const RoutineRef& routine) {
    ScriptState& s = block();
    switch (routine.id()) {
    case kTutorialSceneStep:
        if (s.stepPhase == 0) {
            scriptFade(kSceneFadeIn, 0);
            ++s.stepPhase;
        } else if (s.stepPhase == 1 && fadeState().idle()) {
            s.flags &= uint8_t(~kScriptUnskippable);
            s.stepPhase = 2;
        }
        return;
    default: notPortedAt(kStoryPort, "script 0x4b step", routine.pointerValue());
    }
}
void tutorialScene() {
    ScriptState& s = block();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x530030
        switch (s.subState) {
        case 0:
            scriptFade(kSceneFadeOut, 0);
            s.flags |= kScriptUnskippable;
            scriptReplayPadOn();
            ++s.subState;
            return;
        case 1:
            if (!fadeState().idle()) return;
            ++s.subState;
            return;
        case 2:
            actorsSweep(kSweepActors, kSweepFree);
            windowClose(kTutorialWindow, WindowClose::Force);
            scriptSequenceStart(stateAddress(TableId::IceRuinScriptsTutorialSceneCamera), stateAddress(TableId::IceRuinScriptsTutorialSceneSteps));
            playerAt(kTutorialX, kTutorialY, kTutorialZ, kTutorialYaw);
            ++s.subState;
            [[fallthrough]];
        case 3:
            if (playerToScriptIfFree() != 0) return;
            soundFadeOut(kScenesSoundMask, kScenesSoundFade, 0);
            s.enterState(CutsceneState::Steps);
            playerScriptAnim(PlayerAnim::Idle, 0, true);
            return;
        default: return;
        }
    case CutsceneState::Steps: runStepsFadingAtEnd(&tutorialSceneStep); return; // exe 0x530140
    case CutsceneState::WrapUp:                                                 // exe 0x5301b0
        switch (s.subState) {
        case 0:
            if (fadeState().idle()) s.subState = 1;
            return;
        case 1:
            s.subState = 2;
            requestTransition(kTransitionAccepted, kSt0bStage, kSt0bRoom, 0xc0, -0x20, 0x180, 0x800, kSceneFadeIn,
                              kNoFade);
            actorsSweep(kSweepSecondGroups, kSweepFree);
            return;
        case 2:
            if (transitionRequest().state != kTransitionNone) return;
            playerBackToPlay(); // exe 0x5723c0
            scriptReplayPadOff();
            player().flags |= kActorDrawn;
            scriptEndBlock();
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x4b state", s.state);
    }
}

} // namespace

bool iceRuinScriptPorted(RoutineId handler) {
    switch (handler) {
    case RoutineId::IceRuinShrinkScript:
    case RoutineId::IceRuinCritterScene:
    case RoutineId::IceRuinTutorialScript:
    case RoutineId::IceRuinTutorialScene: return true;
    default: return false;
    }
}

bool iceRuinScriptTick(const PlayerEnv&, RoutineId handler) {
    switch (handler) {
    case RoutineId::IceRuinShrinkScript: shrinkScript(); return true;
    case RoutineId::IceRuinCritterScene: critterScene(); return true;
    case RoutineId::IceRuinTutorialScript: tutorialScript(); return true;
    case RoutineId::IceRuinTutorialScene: tutorialScene(); return true;
    default: return false;
    }
}

} // namespace od::port
