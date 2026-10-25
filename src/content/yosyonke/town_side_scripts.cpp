// The town's side scripts 0x27 (ST0B), 0x70 (ST06), 0x72 (ST09) and 0x73 (ST47) and their cast. The shadow list
// (exe 0x5f20f0) is render output and left out (as the other cast ports).
//
// Scripts:
//   0x27 (exe 0x524540)  ST0B (the licence exam office, room case 0x57d050 on flag 0x80): the doors between sub 0 and
//                        sub 1 swing open (records 0x8a46b8 / 0x8a46cc or 0x8a46e0 / 0x8a46f4, yaw +-), the player
//                        walks through, the request to the other sub (0 -> 1 / 1 -> 0)
//   0x70 (exe 0x53d060)  ST06 sub 5 (case 0x57c5a0, flag 0xd3): voice 0xff03, then the request to ST06 sub 3; the end
//                        takes 500 from the tool-kit balance and sets flag 0xde
//   0x72 (exe 0x53d3b0)  ST09 (case 0x57cad0, once: flag 0x5b0): message 0x5a with the player's head turn, cast
//                        0x448e40
//   0x73 (exe 0x53d640)  ST47 (case 0x58ac10, flag 0x680): a door leaf (static type 1 variant 3) opens, the player
//                        walks in, messages 0x78 / 0x79, cast 0x449270 (walks off) / 0x5ba960 (rises, flies off); the
//                        request to ST47 sub 0
#include "content/yosyonke/town_side_scripts.hpp"
#include "content/yosyonke/town_side_scripts_state.hpp"

#include "content/yosyonke/town_puppet.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/cutscene_actor.hpp"
#include "game/display.hpp"
#include "game/door_leaves.hpp"
#include "game/fade_state.hpp"
#include "game/game_state.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/message.hpp"
#include "game/message_state.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/rewards.hpp"
#include "game/room_grid.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/spawn.hpp"
#include "game/stage_files.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/transition.hpp"
#include "game/transition_state.hpp"

using namespace od::port::town_side_scripts_state;

namespace od::port {

using namespace town_side_scripts_state;

namespace {

ScriptState& block() { return scriptState(); }
int32_t stepTick() { return block().stepTick; }

// the "next step" tail of the step routines: the sub-state and the phase bytes cleared, the list one step ahead
void nextStep() {
    block().subState = 0;
    scriptNextStep();
}
// the cell word of (column, row) as the room code inlines it
uint16_t cellWord(uint32_t column, uint32_t row) { return GridCell(gridCellAtIndex(column, row)).word; }
void moveFacing(int16_t z) { moveInFacing(player(), 0, 0, z); } // exe 0x571910(player, 0, 0, z)
// the player's walk-out of the door scripts (exe 0x53d9f0 / 0x524970): tick 0 the replay pad holds forward and walk,
// the animation (2, 8); tick 1 exe 0x5723c0; from tick `fast` an extra step `run`; a step `walk` every tick
void walkOut(int32_t t, int32_t fast, int16_t run, int16_t walk, bool turn) {
    Player& p = player();
    if (t == 0) {
        replayPad().held = uint16_t(p.button(PlayerButton::Forward) | p.button(PlayerButton::Walk));
        playerScriptAnimId(2, 8, 1); // exe 0x5723d0
        if (turn) {                  // exe 0x53d9f0 only: the yaw + 0xe3, the previous rotation copied
            p.rotation.y += 0xe3u;
            p.prevRotation = p.rotation;
        }
    } else if (t == 1) {
        playerBackToPlay(); // exe 0x5723c0
    } else if (t >= fast) {
        moveFacing(run);
    }
    moveFacing(walk);
}
// the face tracks of a script on its two tables, both from key 0 of sequence 0 (exe 0x5e33a0, 0x5e33d0, 0x5e33f0)
void faceTracksStart(uint32_t eyes, uint32_t mouth) {
    KeyframeTracks& tracks = block().faceTracks;
    tracks.init(eyes, mouth);
    tracks.first.select(0, 0);
    tracks.second.select(0, 0);
}
// exe 0x53f090: the step runner without a skip: the step or, at the list's end, the wrap up; the sequence's tick
void stepsWithoutSkip(ScriptStepRoutines routines) {
    ScriptState& s = block();
    const ScriptStep step = scriptStepAt(s.steps.value());
    if (step.id != kScriptStepEnd) {
        if (const RoutineRef routine = stateRoutine(step.routine)) routines(routine);
    } else {
        s.enterState(CutsceneState::WrapUp);
    }
    scriptSequenceTick();
}

// ==== script 0x27 (exe 0x524540, states 0x8a47e8; steps 0x8a47a0) =================================================
// ST0B, the licence exam office: the room case 0x57d050 runs it on flag 0x80 (no event). Sub 0: camera 0x8a4708, the
// destination sub 1, the player at (0, -0x380, 0x3b0) yaw 0x800, the door leaves 0x8a46b8 / 0x8a46cc, the door lists of
// the cells (0x3f, 0x42) / (0x40, 0x42) hidden; sub 1: camera 0x8a4754, the destination sub 0, the player at (0, 0,
// -0x150) yaw 0, the leaves 0x8a46e0 / 0x8a46f4 (the other way round), the cell (0x3f, 0x3e). Then the player scripted,
// the DAT loader idle, voice 0xffe00010. Steps: 0x524950 (anim 0), 0x52ef00 x 2, 0x524970 (the walk-out); every step
// the leaves swing (exe 0x524a00). End: the request (state 3) to ST0B's destination sub, the door lists back, sound
// 0x1c / the fade 0x222.
const DataTable<RoutineTable<3>> examDoorStates{TableId::TownSideScriptsExamDoorStates};
constexpr size_t kLeftLeaf = 0, kRightLeaf = 1; // the script's actors
constexpr uint8_t kST0B = 0xb;
void examDoorsStep(const RoutineRef& routine) {
    const int32_t t = stepTick();
    switch (routine.id()) {
    case RoutineId::TownExamDoorsStep1:
        if (t == 0) playerScriptAnimId(0, 0, 1);
        return;
    case RoutineId::EmptyRoutine: return; // ret
    case RoutineId::TownExamDoorsWalkOutStep: walkOut(t, 0x14, -0x1e, -8, false); return;
    default: notPortedAt(kStoryPort, "script 0x27 step", routine.pointerValue());
    }
}
// exe 0x524a00: while the sequence's ticks are in 10..0x78 the leaves turn by min(ticks >> 3, 8) in opposite
// directions
void swingExamDoors() {
    const int32_t c = block().totalTicks;
    if (c < 10 || c > 0x78) return;
    int16_t v = int16_t(c >> 3);
    if (v >= 9) v = 8;
    Actor& l = actorLayoutOf(*block().actors[kLeftLeaf]);
    Actor& r = actorLayoutOf(*block().actors[kRightLeaf]);
    l.rotation.y = int16_t(uint16_t(l.rotation.y + v));
    r.rotation.y = int16_t(uint16_t(r.rotation.y - v));
}
void examDoorsScript() {
    ScriptState& s = block();
    ExamDoorsBytes& f = scriptState().part<ExamDoorsBytes>();
    switch (examDoorStates->idAt(s.state)) {
    case RoutineId::TownSideExamDoorsSetUp:
        switch (s.subState) {
        case 0: {
            actorsSweep(kSweepTransition, kSweepFree); // exe 0x5e05d0
            windowCloseAll(WindowClose::Force);        // exe 0x5577d0
            s.clearScriptByteRun();                    // exe 0x40dc40
            Player& p = player();
            if (gameState().room == 1) {
                f.destinationRoom = 0;
                scriptSequenceStart(stateAddress(TableId::TownSideScriptsExamDoorsOutCameraScript), stateAddress(TableId::TownSideScriptsExamDoorsSteps)); // exe 0x511390
                scriptReplayPadOn();                     // exe 0x5117f0
                p.pos.x.whole = 0;
                p.pos.y.whole = 0;
                p.pos.z.whole = -0x150;
                p.rotation.y = 0;
                pointAtWord(s.actors[kRightLeaf], spawnRecordTracked(stateAddress(TableId::TownSideScriptsExamDoorsOutRightLeaf)));
                pointAtWord(s.actors[kLeftLeaf], spawnRecordTracked(stateAddress(TableId::TownSideScriptsExamDoorsOutLeftLeaf)));
                f.doorSegmentA = cellWord(0x3f, 0x3e);
                segmentSetDoorsHidden(f.doorSegmentA, true); // exe 0x600f50
                scriptSequenceTick();
                ++s.subState;
                return;
            }
            if (gameState().room == 0) {
                f.destinationRoom = 1;
                scriptSequenceStart(stateAddress(TableId::TownSideScriptsExamDoorsInCameraScript), stateAddress(TableId::TownSideScriptsExamDoorsSteps));
                scriptReplayPadOn();
                p.pos.x.whole = 0;
                p.pos.y.whole = -0x380;
                p.pos.z.whole = 0x3b0;
                p.rotation.y = kHalfTurn;
                pointAtWord(s.actors[kLeftLeaf], spawnRecordTracked(stateAddress(TableId::TownSideScriptsExamDoorsInLeftLeaf)));
                pointAtWord(s.actors[kRightLeaf], spawnRecordTracked(stateAddress(TableId::TownSideScriptsExamDoorsInRightLeaf)));
                f.doorSegmentA = cellWord(0x3f, 0x42);
                f.doorSegmentB = cellWord(0x40, 0x42);
                segmentSetDoorsHidden(f.doorSegmentA, true);
                segmentSetDoorsHidden(f.doorSegmentB, true);
            }
            scriptSequenceTick();
            ++s.subState;
            return;
        }
        case 1:
            if (playerToScriptIfFree() != 0) return; // exe 0x572300
            ++s.subState;
            return;
        case 2:
            if (fileLoads().loading & kFileLoading) return;
            ++s.subState;
            return;
        case 3:
            if (fileLoads().loading & kStreamBusy) return;
            voiceClipPlay(0xffe00010u); // exe 0x601050
            ++s.subState;
            return;
        case 4:
            if (!voiceClipPlaying()) return; // exe 0x601240
            s.enterState(CutsceneState::Steps);
            return;
        default: return;
        }
    case RoutineId::TownSideExamDoorsSteps: { // the runner: the cast's copy of the timeline, the step, the leaves
        scriptState().part<CastTimelineStep>().stepId = s.stepId;
        scriptState().part<CastTimelineStep>().stepSub = s.stepSub;
        scriptState().part<CastTimelineTick>().stepTick = s.stepTick;
        const ScriptStep step = scriptStepAt(s.steps.value());
        if (step.id != kScriptStepEnd) {
            examDoorsStep(stateRoutine(step.routine)); // called unchecked
            swingExamDoors();
            scriptSequenceTick();
            return;
        }
        s.state = uint8_t(CutsceneState::WrapUp);
        scriptSequenceTick();
        return;
    }
    case RoutineId::TownSideExamDoorsWrapUp:
        switch (s.subState) {
        case 0:
            padVibrate(0, 0x1000000); // exe 0x55cd10
            ++s.subState;
            [[fallthrough]];
        case 1: {
            ++s.subState;
            TransitionRequest& r = transitionRequest();
            r.state = kTransitionScriptDone;
            r.keepPosition = 0;
            r.exitId = 0;
            r.exitByte1 = 0;
            r.destStage = kST0B;
            r.destRoom = f.destinationRoom;
            r.destination.x = 0;
            if (f.destinationRoom != 0) {
                r.destination.y = 0;
                r.destination.z = -0x1d0;
                r.destination.yaw = kHalfTurn;
            } else {
                r.destination.y = -0x380;
                r.destination.z = 0x380;
                r.destination.yaw = 0;
            }
            r.fadeOut = kNoFade;
            r.fadeIn = kNoFade;
            return;
        }
        case 2:
            if (transitionRequest().state != kTransitionNone) return;
            replayPad().held = 0;
            if (f.destinationRoom != 0) {
                segmentSetDoorsHidden(f.doorSegmentA, false);
                segmentSetDoorsHidden(f.doorSegmentB, false);
                soundPlayFlatForced(0x1c); // exe 0x5d5260
            } else {
                segmentSetDoorsHidden(f.doorSegmentA, false);
                soundFadeOut(0xf, 0x222, 0); // exe 0x5d5780
            }
            scriptReplayPadOff(); // exe 0x511890
            scriptEndBlock();     // exe 0x5116a0
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x27 state", s.state);
    }
}

// ==== script 0x70 (exe 0x53d060, states 0x8ae220; camera 0x8ae1b0, steps 0x8ae200) =================================
// ST06 sub 5 (case 0x57c5a0) while flag 0xd3: the player at (0x100, 0, 0x80) yaw 0, the replay pad. Steps: 0x53d140
// (voice 0xff03; once it plays: unskippable off, next), 0x53d190 (the voice over, no fade: the request (state 2) to
// ST06 sub 3 at x 0x120 z -0x140 (y / yaw left), fades 2 / none), 0x53d200 (the request taken, no fade: next). End:
// the actors swept, the tool-kit balance - 500 (exe 0x5e5260), flag 0xde, the pad back, the block ended.
const DataTable<RoutineTable<3>> st06VoiceStates{TableId::TownSideScriptsSt06VoiceStates};
constexpr uint8_t kST06 = 6;
void st06VoiceStep(const RoutineRef& routine) {
    ScriptState& s = block();
    switch (routine.id()) {
    case RoutineId::TownSt06VoiceStep1:
        if (s.stepPhase == 0) {
            voiceClipPlay(0xff03);
            ++s.stepPhase;
        } else if (s.stepPhase == 1 && voiceClipPlaying()) {
            s.stepPhase = 0;
            s.flags &= uint8_t(~kScriptUnskippable);
            scriptSequenceSkip(1);
        }
        return;
    case RoutineId::TownSt06VoiceStep2: {
        if (!voiceClipIdle() || fadeState().busy != 0) return; // exe 0x601260
        TransitionRequest& r = transitionRequest();
        r.state = kTransitionAccepted;
        r.keepPosition = 0;
        r.exitId = 0;
        r.exitByte1 = 0;
        r.destStage = kST06;
        r.destRoom = 3;
        r.destination.x = 0x120;
        r.destination.z = -0x140;
        r.fadeOut = kNoFade;
        r.fadeIn = 2;
        scriptSequenceSkip(1);
        return;
    }
    case RoutineId::TownSt06VoiceStep3:
        if (transitionRequest().state == kTransitionNone && fadeState().busy == 0) scriptSequenceSkip(1);
        return;
    default: notPortedAt(kStoryPort, "script 0x70 step", routine.pointerValue());
    }
}
void st06VoiceScript() {
    ScriptState& s = block();
    switch (st06VoiceStates->idAt(s.state)) {
    case RoutineId::TownSideSt06VoiceSetUp: {
        actorsSweep(kSweepActors, kSweepFree);
        scriptSequenceStart(stateAddress(TableId::TownSideScriptsSt06VoiceCameraScript), stateAddress(TableId::TownSideScriptsSt06VoiceSteps));
        scriptReplayPadOn();
        Player& p = player();
        p.pos.x = 0x1000000;
        p.pos.y = 0;
        p.rotation.y = 0;
        p.pos.z = 0x800000;
        s.enterState(CutsceneState::Steps);
        s.flags |= kScriptUnskippable;
        scriptSequenceTick(); // jmp 0x5114d0
        return;
    }
    case RoutineId::ScriptSequenceState: stepsWithoutSkip(st06VoiceStep); return;
    case RoutineId::TownSideSt06VoiceWrapUp:
        switch (s.subState) {
        case 0:
            ++s.subState;
            [[fallthrough]];
        case 1:
            ++s.subState;
            actorsSweep(kSweepActors, kSweepFree);
            return;
        case 2:
            toolKitBalanceAdd(-500); // exe 0x5e5260
            setStoryFlag(0xde);
            scriptReplayPadOff();
            scriptEndBlock(); // jmp 0x5116a0
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x70 state", s.state);
    }
}

// ==== script 0x72 (exe 0x53d3b0, states 0x8ae370; camera 0x8ae284 (op 0x40: record 0x8ae270 = cast 0x448e40), steps
// 0x8ae300, face tracks 0x8ae360 / 0x8ae368) ========================================================================
// ST09 (case 0x57cad0, once: flag 0x5b0). The player at (0x12a0, 0, -0x1bf0) yaw 0xab0. Steps: 0x53d540 (next at
// 0x3c), 0x53d570 (anim 0, message 0x5a of the stage's bank; next once closed), 0x53d5d0 (the head toward 0x300 by
// 0x60; next at 0x28), 0x53d610 (next at 10). End: fade 0x12 (unless skipped), the windows closed (fade 1 if dark),
// the player back and drawn, the block ended.
const DataTable<RoutineTable<3>> bystanderScriptStates{TableId::TownSideScriptsBystanderScriptStates};
void bystanderStep(const RoutineRef& routine) {
    ScriptState& s = block();
    const int32_t t = stepTick();
    switch (routine.id()) {
    case RoutineId::TownBystanderStep1:
        if (s.subState == 0 && t == 0x3c) nextStep();
        return;
    case RoutineId::TownBystanderStep2:
        if (s.subState == 0) {
            playerScriptAnimId(0, 0, 1);
            windowOpen(0, kMessageBank, 0x5a); // exe 0x557390
            s.subState = 1;
        } else if (s.subState != 1) {
            return;
        }
        if (windowStatus(0) != 0) return; // exe 0x557810
        nextStep();
        return;
    case RoutineId::TownBystanderStep3:
        if (s.subState == 0) {
            ScriptHeadTarget& head = scriptState().part<ScriptHeadTarget>();
            head.yaw = 0x300;
            head.step = 0x60;
            s.subState = 1;
        } else if (s.subState != 1) {
            return;
        }
        if (t == 0x28) nextStep();
        return;
    case RoutineId::TownBystanderStep4:
        if (s.subState == 0 && t == 10) nextStep();
        return;
    default: notPortedAt(kStoryPort, "script 0x72 step", routine.pointerValue());
    }
}
void bystanderScript() {
    ScriptState& s = block();
    switch (bystanderScriptStates->idAt(s.state)) {
    case RoutineId::TownSideBystanderSetUp:
        if (s.subState == 0) {
            scriptSequenceStart(stateAddress(TableId::TownSideScriptsBystanderCameraScript), stateAddress(TableId::TownSideScriptsBystanderSteps));
            scriptReplayPadOn();
            Player& p = player();
            p.pos.x.whole = 0x12a0;
            p.pos.y.whole = 0;
            p.pos.z.whole = -0x1bf0;
            p.rotation.y = 0xab0;
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
            faceTracksStart(stateAddress(TableId::TownSideScriptsBystanderEyes), stateAddress(TableId::TownSideScriptsBystanderMouth));
            s.enterState(CutsceneState::Steps);
        }
        scriptSequenceTick();
        return;
    case RoutineId::TownSideBystanderSteps: scriptStepsStateHeadTurn(bystanderStep); return;
    case RoutineId::TownSideBystanderWrapUp:
        switch (s.subState) {
        case 0:
            padVibrate(0, 0x1000000);
            ++s.subState;
            if (!(s.flags & kScriptSkipTaken)) scriptFade(0x12, 0);
            s.subState = 2;
            return;
        case 1: s.subState = 2; return;
        case 2:
            if (fadeState().busy != 0) return;
            windowCloseAll(WindowClose::Force);
            s.subState = 3;
            if (fadeState().covered != 0) scriptFade(1, 0);
            return;
        case 3:
            playerBackToPlay(); // exe 0x5723c0
            scriptReplayPadOff();
            player().flags |= kActorDrawn;
            scriptEndBlock();
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x72 state", s.state);
    }
}

// ==== script 0x73 (exe 0x53d640, states 0x8ae548; camera 0x8ae3bc (op 0x40: records 0x8ae380 cast 0x449270 variant 0,
// 0x8ae394 cast 0x449270 variant 1 -> actor 1, 0x8ae3a8 cast 0x5ba960 -> actor 2), steps 0x8ae4b0, face tracks
// 0x8ae538 / 0x8ae540) ===============================================================================================
// ST47 (case 0x58ac10 on flag 0x680, no event). State 0: the actors of the transitions swept, the windows closed, a
// door leaf (static type 1 variant 3, the hinged door 0x5628b0) at (0, 0, -0x160) -> actor 3, the door list of its cell
// hidden, the player beside it (offset by the bytes 0x8ae554 / 0x8ae555) on the ground; once scripted the face tracks
// and the sound fade 0x111. Steps: 0x53d970 (anim 0x53), 0x53d990 (the leaf follows the hand), 0x53d9b0 (the leaf
// swings on: its opening / 0x11, -2), 0x53d9f0 (the walk-in), 0x53daa0 (the player placed at the origin, message
// 0x78; actor 1 walks off), 0x53db50 (sounds 0xb8 / 0xb9, record 0x8ae394 freed, a 0x50 wait), 0x53dc70 (message 0x79;
// actor 2 rises), 0x53dcd0 (actor 2 flies off at 0x5a), 0x53dd00 (the end at 100). End: fade 0x12, the request (state
// 2) to ST47 sub 0 at (-0x120, 0, -0x80) yaw 0xc00, then the door list back, flag 0x710 cleared, sound 0x16.
const DataTable<RoutineTable<3>> visitorScriptStates{TableId::TownSideScriptsVisitorScriptStates};
constexpr size_t kWalkerActor = 1, kFlyerActor = 2, kLeafActor = 3; // the script's actors
const DataTable<ScalarTable<int8_t, 2>> walkInOffsets{TableId::TownSideScriptsWalkInOffsets}; // the player's offset beside the leaf (two cell-frame factors)
constexpr uint8_t kST47 = 0x47;
constexpr uint8_t kVisitorLeafVariant = 3;
// the cue byte (+0xd) of the cast the steps set
struct CastCue {
    explicit CastCue(const Actor& actor) : CastCue(const_cast<Actor&>(actor)) {}
    explicit CastCue(Actor& actor) : go(actor.params[1]) {}
    uint8_t& go;
};

// exe 0x53d870
void visitorWrapUp() {
    ScriptState& s = block();
    VisitorBytes& f = scriptState().part<VisitorBytes>();
    switch (s.subState) {
    case 0:
        padVibrate(0, 0x1000000);
        ++s.subState;
        if (!(s.flags & kScriptSkipTaken)) scriptFade(0x12, 0);
        [[fallthrough]];
    case 1:
        if (fadeState().busy != 0) return;
        windowCloseAll(WindowClose::Force);
        s.subState = 2;
        requestTransition(kTransitionAccepted, kST47, 0, -0x120, 0, -0x80, 3 * kQuarterTurn, 2, kNoFade);
        return;
    case 2:
        if (transitionRequest().state != kTransitionNone) return;
        replayPad().held = 0;
        segmentSetDoorsHidden(f.doorSegment, false);
        scriptReplayPadOff();
        player().flags |= kActorDrawn;
        scriptEndBlock();
        clearStoryFlag(kFlagLocked);
        soundPlayFlatForced(0x16);
        return;
    default: return;
    }
}
void visitorStep(const RoutineRef& routine) {
    ScriptState& s = block();
    VisitorBytes& f = scriptState().part<VisitorBytes>();
    const int32_t t = stepTick();
    switch (routine.id()) {
    case RoutineId::TownVisitorStep1:
        if (t == 0) playerScriptAnimId(0x53, 0, 1);
        return;
    case RoutineId::TownVisitorStep2:
        if (t == 0) asStatic(*s.actors[kLeafActor]).subState = uint8_t(SwingLeafSub::FollowHand);
        return;
    case RoutineId::TownVisitorStep3: {
        if (t != 0) return;
        StaticActor& leaf = asStatic(*s.actors[kLeafActor]);
        SwingLeafFields swing(leaf);
        leaf.subState = uint8_t(SwingLeafSub::SwingOn);
        leaf.spin.y = int16_t(int(swing.opening) / 0x11);
        swing.swingAcceleration = -2;
        return;
    }
    case RoutineId::ExitHingedWalkThroughStep: walkOut(t, 0x11, -0x28, -0xc, true); return;
    case RoutineId::TownVisitorStep4:
        switch (s.subState) {
        case 0: {
            replayPad().held = 0;
            playerScriptAnimId(0, 0, 1);
            Player& p = player();
            p.pos.x.whole = 0;
            p.setDrawn(false);
            p.pos.y.whole = 0;
            p.pos.z.whole = 0;
            f.timer = 0x14;
            s.subState = 1;
            [[fallthrough]];
        }
        case 1:
            --f.timer;
            if (f.timer != 0) return;
            windowOpen(0, kMessageBank, 0x78);
            s.subState = 2;
            return;
        case 2:
            if (windowStatus(0) != 0) return;
            nextStep();
            CastCue(actorLayoutOf(*s.actors[kWalkerActor])).go = 1;
            return;
        default: return;
        }
    case RoutineId::TownVisitorStep5:
        if (s.subState == 1) {
            nextStep();
            return;
        }
        switch (s.stepSubPhase) {
        case 0:
            f.soundTimer = 0x50;
            ++s.stepSubPhase;
            return;
        case 1:
        case 2: {
            --f.soundTimer;
            if (f.soundTimer != 0) return;
            const bool first = s.stepSubPhase == 1;
            if (first) f.soundTimer = 0x1e;
            soundPlayAtXyz(first ? 0xb8 : 0xb9, -0x1b0, 0, 0x100); // exe 0x5d5380 (a stack vector)
            ++s.stepSubPhase;
            return;
        }
        case 3:
            scriptActorFreeRecord(stateAddress(TableId::TownSideScriptsVisitorRecord)); // exe 0x511760
            f.timer = 0x50;
            ++s.stepSubPhase;
            return;
        case 4:
            --f.timer;
            if (f.timer != 0) return;
            s.subState = 1;
            ++s.stepSubPhase;
            return;
        default: return;
        }
    case RoutineId::TownVisitorStep6:
        if (s.subState == 0) {
            if (t == 0x78) {
                windowOpen(0, kMessageBank, 0x79);
                s.subState = 1;
            }
        } else if (s.subState == 1 && windowStatus(0) == 0) {
            nextStep();
            CastCue(actorLayoutOf(*s.actors[kFlyerActor])).go = 1;
        }
        return;
    case RoutineId::TownVisitorStep7:
        if (s.subState == 0 && t == 0x5a) {
            nextStep();
            CastCue(actorLayoutOf(*s.actors[kFlyerActor])).go = 1;
        }
        return;
    case RoutineId::TownVisitorStep8:
        if (s.subState == 0 && t == 100) {
            s.enterState(CutsceneState::WrapUp);
            visitorWrapUp();
        }
        return;
    default: notPortedAt(kStoryPort, "script 0x73 step", routine.pointerValue());
    }
}
// exe 0x53d660
void visitorSetUp(const PlayerEnv& env) {
    ScriptState& s = block();
    VisitorBytes& f = scriptState().part<VisitorBytes>();
    if (s.subState == 0) {
        actorsSweep(kSweepTransition, kSweepFree);
        windowCloseAll(WindowClose::Force);
        s.clearScriptByteRun();
        const uint32_t at = allocActor60(0); // exe 0x5e1300
        if (at == 0) return;
        StaticActor& leaf = staticActorAt(at);
        leaf.flags = kActorLiveDrawn;
        leaf.type = kHingedLeafType;
        leaf.variant = kVisitorLeafVariant;
        leaf.setParamsWord(0);
        leaf.pos.x = 0;
        leaf.pos.y = 0;
        leaf.pos.z = int32_t(0xfea00000u);
        s.actors[kLeafActor] = &leaf;
        scriptSequenceStart(stateAddress(TableId::TownSideScriptsVisitorCameraScript), stateAddress(TableId::TownSideScriptsVisitorSteps));
        scriptReplayPadOn();
        // the stack copy of the leaf's position
        uint32_t x = uint32_t(leaf.pos.x), y = uint32_t(leaf.pos.y), z = uint32_t(leaf.pos.z);
        const uint32_t cell = gridCellOf(int16_t(x >> 16), int16_t(z >> 16)); // exe 0x40f030
        if (cell == 0) notPortedAt(kStoryPort, "script 0x73: exe 0x40f030 returned null (the exe reads [0])", 0);
        const uint16_t segment = GridCell(cell).word & kCellSegmentMask;
        f.doorSegment = segment;
        segmentSetDoorsHidden(segment, true);
        const int c5 = walkInOffsets[1], c4 = walkInOffsets[0];
        x = (x & 0xffffu) | uint32_t(uint16_t((x >> 16) + uint32_t(c5 * 0x7c - c4 * 0x24))) << 16;
        z = (z & 0xffffu) | uint32_t(uint16_t((z >> 16) + uint32_t(-c5 * 0x24 - c4 * 0x7c))) << 16;
        Player& p = player();
        p.pos.x = int32_t(x);
        p.pos.y = int32_t(y);
        p.pos.z = int32_t(z);
        p.prevPos.x = int32_t(x);
        p.prevPos.y = int32_t(y);
        p.prevPos.z = int32_t(z);
        if (actorTestMove(env, p) != 0) actorPushOut(env, p); // exe 0x40f570 / 0x410960
        y = (y & 0xffffu) | uint32_t(uint16_t(actorGround(env, p))) << 16; // exe 0x40f190
        p.pos.x = int32_t(x); // the copies taken before the push-out
        p.pos.z = int32_t(z);
        p.prevPos.x = int32_t(x);
        p.prevPos.z = int32_t(z);
        p.rotation.z = 0;
        const uint32_t zSpare = p.rotation.zSpareWord();
        p.rotation.x = 0;
        p.rotation.y = 0;
        p.pos.y = int32_t(y);
        p.prevPos.y = int32_t(y);
        p.prevRotation.setWords(p.rotation.xyWord(), zSpare);
        s.subState = 1;
    } else if (s.subState != 1) {
        return;
    }
    if (playerToScriptIfFree() != 0) return;
    faceTracksStart(stateAddress(TableId::TownSideScriptsVisitorEyes), stateAddress(TableId::TownSideScriptsVisitorMouth));
    s.enterState(CutsceneState::Steps);
    soundFadeOut(0xf, 0x111, 0);
}
void visitorScript(const PlayerEnv& env) {
    switch (visitorScriptStates->idAt(block().state)) {
    case RoutineId::TownSideVisitorSetUp: visitorSetUp(env); return;
    case RoutineId::ScriptStepsState: scriptStepsState(visitorStep); return;
    case RoutineId::TownSideVisitorWrapUp: visitorWrapUp(); return;
    default: notPortedAt(kStoryPort, "script 0x73 state", block().state);
    }
}

// ==== the cast ====================================================================================================
// The town puppets 0x448e40 / 0x449270 (their class bytes, state 0 and tail: content/yosyonke/town_puppet)

// ---- the ST09 bystander, class 0x448e40 (pool 0x20 type 0 sub 3: script 0x72 record 0x8ae270; variant 0 -> 0x449070)
// exe 0x449070 (the variant table 0x88e320, box 0x88e328): Idle anim 0 until the step sub 1, Watch anim 3 (the animation
// free) until sub 2, Leave anim 2 key 6 turning to 0x400 and moving along it (0x100 a tick); any state but Exit goes to
// Exit when the script reaches its wrap up: anim 2 placed at (0xb00, 0, -0x1d00) yaw 0x400, walks (turning to 0x380) to
// x <= 0x1cf, then turns to 0xc0 and walks off until z <= -0x3001 (freed).
enum class BystanderState : uint8_t { Bind = 0, Idle = 1, Watch = 2, Leave = 3, Exit = 4 };
void bystander(AnimatedActor& a) {
    PuppetFields& f = classState<PuppetFields>(a);
    const ScriptState& s = block();
    switch (BystanderState(a.state)) {
    case BystanderState::Bind:
        if (!puppetBind(a, stateAddress(TableId::TownSideScriptsBystanderBox), 2)) return;
        f.puppetFlags |= kPuppetResetAnim;
        break;
    case BystanderState::Idle:
        if (a.step == 0) {
            a.anim = 0;
            a.step = 1;
        }
        if (s.stepSub == 1) a.enterState(uint8_t(BystanderState::Watch));
        break;
    case BystanderState::Watch:
        if (a.step == 0) {
            a.anim = 3;
            f.puppetFlags &= uint8_t(~kPuppetResetAnim);
            a.step = 1;
        }
        if (s.stepSub == 2) a.enterState(uint8_t(BystanderState::Leave));
        break;
    case BystanderState::Leave:
        if (a.step == 0) {
            f.animKey = 6;
            a.anim = 2;
            f.puppetFlags &= uint8_t(~kPuppetResetAnim);
            a.step = 1;
        } else if (a.step != 1) {
            break;
        }
        turnYawTo(a.rotation.y, 0x400, 0x40);     // exe 0x5e3df0
        moveAlongYaw(a.pos, 0x400, 0, 0, -0x100); // exe 0x5e36f0
        break;
    case BystanderState::Exit:
        if (a.step == 2) {
            turnYawTo(a.rotation.y, 0xc0, 0x80);
            integrateVelocity(a, 0, 0, -0x180); // exe 0x5e3690
            if (a.pos.z.whole <= -0x3001) freeActor(a);
            break;
        }
        if (a.step == 0) {
            f.puppetFlags &= uint8_t(~kPuppetResetAnim);
            a.anim = 2;
            a.pos.x.whole = 0xb00;
            a.pos.y.whole = 0;
            a.pos.z.whole = -0x1d00;
            a.rotation.y = kQuarterTurn;
            a.step = 1;
        } else if (a.step != 1) {
            break;
        }
        turnYawTo(a.rotation.y, 0x380, 0x40);
        integrateVelocity(a, 0, 0, -0x180);
        if (a.pos.x.whole <= 0x1cf) a.step = 2;
        break;
    default: break;
    }
    if (BystanderState(a.state) != BystanderState::Exit && s.state == uint8_t(CutsceneState::WrapUp))
        a.enterState(uint8_t(BystanderState::Exit));
}
// exe 0x448e40
void st09Bystander(const PlayerEnv& env, AnimatedActor& a) {
    castSavePose(a);
    if (classState<PuppetFields>(a).variant != 0) notPortedAt(kStoryPort, "0x448e40 variant (table 0x88e320 has 1)", a.params[0]);
    bystander(a);
    if (!(a.flags & kActorLive)) return;
    puppetTail(env, a);
}

// ---- the ST47 visitors, class 0x449270 (pool 0x20 type 0 sub 2: script 0x73 records 0x8ae380 / 0x8ae394) ------------
// The variants [0x88e338 + variant * 4] (box 0x88e340): 0 = 0x4494a0 (anim 8 set again every tick, the head off, its
// target 0xb40), 1 = 0x449530 (anim 0, the head on toward 0xc00 by 0x40; once cued (step 0x53daa0) the head toward its
// yaw, anim 1, then the walk 0x3c a tick)
enum class VisitorVariant : int8_t { Waiting = 0, Walker = 1 };
enum class WalkerSub : uint8_t { Wait = 0, Walk = 1 };
// exe 0x449270
void st47Visitor(const PlayerEnv& env, AnimatedActor& a) {
    PuppetFields& f = classState<PuppetFields>(a);
    castSavePose(a);
    switch (VisitorVariant(f.variant.get())) {
    case VisitorVariant::Waiting: // exe 0x4494a0
        if (a.state != 0) break;
        if (!puppetBind(a, stateAddress(TableId::TownSideScriptsVisitorBox), 8)) return;
        f.puppetFlags |= kPuppetResetAnim;
        f.headYaw = 0;
        f.headPitch = 0;
        f.headTarget = 0xb40;
        f.headHeight = 0;
        f.headOn = 0;
        break;
    case VisitorVariant::Walker: // exe 0x449530
        if (a.state == 0) {
            if (!puppetBind(a, stateAddress(TableId::TownSideScriptsVisitorBox), 0)) return;
            f.puppetFlags &= uint8_t(~kPuppetResetAnim);
            f.headYaw = 0;
            f.headPitch = 0;
            f.headRate = 0x40;
            f.headTarget = 3 * kQuarterTurn;
            f.headHeight = 0;
            f.headOn = 1;
            break;
        }
        if (a.state != 1) break;
        if (WalkerSub(a.subState) == WalkerSub::Wait) {
            if (f.go != 1) break;
            f.headRate = 0x40;
            f.headTarget = a.rotation.y;
            f.headHeight = 0;
            a.anim = 1;
            ++a.subState;
            break;
        }
        if (WalkerSub(a.subState) == WalkerSub::Walk) integrateVelocity(a, 0, 0, -0x3c); // exe 0x5e3690
        break;
    default: notPortedAt(kStoryPort, "0x449270 variant (table 0x88e338 has 2)", a.params[0]);
    }
    if (!(a.flags & kActorLive)) return;
    puppetTail(env, a);
}

// ---- the ST47 flyer, class 0x5ba960 (pool 0x60 stage type 0x59: script 0x73 record 0x8ae3a8) -------------------------
// The states [0x8d0c5c + state * 4] = 0x5ba980 (the model, placed at (0xd8, -0xa8, 0xc0) rotation (0x1a8, 0x800, 0)) /
// 0x5ba9e0 / 0x52ef00 x 2. exe 0x5ba9e0 by its sub-state: Wait for the cue; Rise: placed at (-0x20, -0x1c0, 0x78)
// rotation (0, 0xd00, -0x100), scale 0x800, then Spin (the roll += 10, the yaw += 0x10) rising 6 a tick until cued
// again; Land: placed at (-0x18, -0x10, 0x50) rotation (0xc0a, 0xf00, 0), Glide 4 ticks of (+2, +1, -3) a tick tilting
// (-2, -2), then Still.
enum class FlyerState : uint8_t { Bind = 0, Fly = 1 };
enum class FlyerSub : uint8_t { Wait = 0, Rise = 1, Spin = 2, Land = 3, Glide = 4, Still = 5 };
void flyerFly(StaticActor& a) {
    FlyerFields& f = classState<FlyerFields>(a);
    switch (FlyerSub(a.subState)) {
    case FlyerSub::Wait:
        if (f.go != 0) a.subState = uint8_t(FlyerSub::Rise);
        return;
    case FlyerSub::Rise:
        f.go = 0;
        a.pos.x.whole = -0x20;
        a.pos.y.whole = -0x1c0;
        a.pos.z.whole = 0x78;
        a.rotation.x = 0;
        a.rotation.y = 0xd00;
        a.rotation.z = -0x100;
        f.glideTicks = 0;
        a.scale.x = 0x800;
        a.scale.y = 0x800;
        a.scale.z = 0x800;
        a.subState = uint8_t(FlyerSub::Spin);
        [[fallthrough]];
    case FlyerSub::Spin:
        a.rotation.z += 10u;
        a.rotation.y += 0x10u;
        a.pos.y += 0x60000u;
        if (f.go != 0) a.subState = uint8_t(FlyerSub::Land);
        return;
    case FlyerSub::Land:
        a.pos.x.whole = -0x18;
        a.pos.y.whole = -0x10;
        a.pos.z.whole = 0x50;
        a.rotation.x = 0xc0a;
        a.rotation.y = 0xf00;
        a.rotation.z = 0;
        f.glideTicks = 4;
        a.subState = uint8_t(FlyerSub::Glide);
        return;
    case FlyerSub::Glide:
        a.pos.x += 0x20000u;
        a.pos.z -= 0x30000u;
        a.pos.y += 0x10000u;
        a.rotation.x -= 2u;
        a.rotation.y -= 2u;
        --f.glideTicks;
        if (f.glideTicks == 0) a.subState = uint8_t(FlyerSub::Still);
        return;
    default: return;
    }
}
// exe 0x5ba960
void st47Flyer(StaticActor& a) {
    switch (a.state) {
    case uint8_t(FlyerState::Bind): // exe 0x5ba980
        if (!bindActorModel(a, a.variant)) {
            freeActor(a); // exe 0x5e1540
            return;
        }
        a.pos.x.whole = 0xd8;
        a.pos.y.whole = -0xa8;
        a.pos.z.whole = 0xc0;
        a.rotation.x = 0x1a8;
        a.rotation.y = kHalfTurn;
        a.rotation.z = 0;
        a.state = uint8_t(FlyerState::Fly);
        flyerFly(a);
        return;
    case uint8_t(FlyerState::Fly): flyerFly(a); return;
    case 2:
    case 3: return; // exe 0x52ef00
    default: notPortedAt(kStoryPort, "0x5ba960 state (table 0x8d0c5c has 4)", a.state);
    }
}

} // namespace

bool townSideScriptPorted(uint8_t id) {
    switch (storyScriptHandler(id).id()) {
    case RoutineId::ExamDoorsScript: // script 0x27
    case RoutineId::St06VoiceScript: // 0x70
    case RoutineId::BystanderScript: // 0x72
    case RoutineId::VisitorScript: // 0x73
        return true;
    default: return false;
    }
}

bool townSideScriptTick(const PlayerEnv& env, uint8_t id) {
    switch (storyScriptHandler(id).id()) {
    case RoutineId::ExamDoorsScript: examDoorsScript(); return true;
    case RoutineId::St06VoiceScript: st06VoiceScript(); return true;
    case RoutineId::BystanderScript: bystanderScript(); return true;
    case RoutineId::VisitorScript: visitorScript(env); return true;
    default: return false;
    }
}

void st09BystanderUpdate(const PlayerEnv& env, uint32_t actor) { st09Bystander(env, animatedActorAt(actor)); }
void st47VisitorUpdate(const PlayerEnv& env, uint32_t actor) { st47Visitor(env, animatedActorAt(actor)); }
void st47FlyerUpdate(const PlayerEnv&, uint32_t actor) { st47Flyer(staticActorAt(actor)); }

} // namespace od::port
