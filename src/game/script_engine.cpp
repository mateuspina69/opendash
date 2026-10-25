// The script engine (see script_engine.hpp).
#include "game/loaded_data.hpp"
#include "game/script_engine.hpp"

#include "game/actor_pools.hpp"
#include "game/audio_logic.hpp"
#include "game/display.hpp"
#include "game/fade_state.hpp"
#include "game/game_state.hpp"
#include "game/camera_script.hpp"
#include "game/camera_state.hpp"
#include "game/event_state.hpp"
#include "game/exit_transitions.hpp"
#include "game/message.hpp"
#include "game/event.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/room_params.hpp"
#include "game/script_state.hpp"
#include "game/spawn.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"
#include "game/transition.hpp"

namespace od::port {

namespace {

constexpr const char* kScriptArea = "script engine port"; // the area of its port-gap errors
constexpr uint8_t kExitTransitionScript = 0; // script 0: the armed exit's transition
constexpr int32_t kCounterCap = 0x7fffffff;  // the sequence counters stop counting there
constexpr uint8_t kSkipRequested = 1; // the block's skip request byte: start was pressed
// the sound listener's formats (exe 0x5d5a60): the camera's eye and view angles, the player's position and rotation
constexpr uint8_t kListenerOnCamera = 1, kListenerOnPlayer = 0;

// the list moved to the step at `step` (exe 0x5114d0 / 0x5115c0's tail): a new id restarts the id's ticks; a skip
// within the id counts one of its ticks
int enterStep(uint32_t step, bool skipped) {
    ScriptState& s = scriptState();
    const ScriptStep next = scriptStepAt(step);
    s.steps.pointAt(step);
    if (s.stepId != next.id) {
        s.stepId = next.id;
        s.idTicks = 0;
        s.stepTick = 0;
        s.stepSub = next.sub;
        return kSequenceNewId;
    }
    if (skipped) ++s.idTicks;
    s.stepSub = next.sub;
    s.stepTick = 0;
    return kSequenceNextStep;
}

} // namespace

void scriptStart(uint8_t id) {
    ScriptState& s = scriptState();
    Player& p = player();
    if (p.hitPoints < 0 || (s.flags & kScriptRunning)) return;
    s.flags = kScriptRunning;
    s.id = id;
    s.stepSub = 0;
    s.stepId = 0;
    s.enterState(0);
    s.clearSavedInputWord();
    s.clearScriptBytes();
    s.stepTick = 0;
    s.idTicks = 0;
    s.totalTicks = 0;
    p.idleFaces[0] = 0;
    s.actors = {};
    s.steps = nullptr;
    s.clearFaceTracks();
    p.idleFaces[1] = 0;
    p.immunity = kImmuneHeld;
    p.runFlags |= kActorRun | kActorShown;
    soundSetListener(ListenerRef::cameraEye(), ListenerRef::cameraAngles(), kListenerOnCamera);
}

void scriptRun(uint8_t id) {
    if (player().hitPoints < 0 || (scriptState().flags & kScriptRunning)) return;
    if (eventState().active != 0) eventAbort();
    windowCloseAll(WindowClose::Force);
    scriptStart(id);
}

void scriptRunnerTick(const PlayerEnv& env) {
    const ScriptState& s = scriptState();
    if (!(s.flags & kScriptRunning)) return;
    const uint8_t id = s.id;
    if (id != kExitTransitionScript) { // [0x89b6d0 + id * 4]: the story / cutscene scripts (game/story)
        if (!storyScriptTick(env, id)) notPortedAt(kScriptArea, "script", id);
        return;
    }
    exitTransitionTick(env);
}

void scriptBlockIdle() {
    ScriptState& s = scriptState();
    if (s.flags == kScriptEnded) s.flags = 0;
}

void scriptSequenceStart(uint32_t cameraScript, uint32_t steps) {
    scriptCameraStart(cameraScript);
    ScriptState& s = scriptState();
    const ScriptStep first = scriptStepAt(steps);
    s.steps.pointAt(steps);
    s.stepId = first.id;
    s.stepSub = first.sub;
}

int scriptSequenceTick() {
    ScriptState& s = scriptState();
    uint8_t f = s.flags;
    if (f & kScriptStepSkipped) {
        s.flags = uint8_t(f & ~kScriptStepSkipped);
        return kSequenceStepSkipped;
    }
    if (!(f & kScriptNoCameraScript)) {
        scriptCameraTick();
        f = s.flags;
    }
    if (f & kScriptCameraPaused) return kSequencePaused;
    const uint32_t step = s.steps.value();
    const ScriptStep current = scriptStepAt(step);
    if (current.id == kScriptStepEnd) return kSequenceEnded;
    for (int32_t* counter : {&s.totalTicks, &s.idTicks, &s.stepTick})
        if (*counter < kCounterCap) ++*counter;
    const int16_t ticks = current.ticks;
    if (ticks == kScriptStepEndless) return kSequenceEndless;
    if (int32_t(ticks) != s.stepTick) return kSequenceRunning;
    return enterStep(step + kScriptStepSize, false);
}

int scriptSequenceSkip(int n) {
    ScriptState& s = scriptState();
    const uint32_t step = s.steps.value();
    if (step == 0) {
        ++s.totalTicks;
        s.flags |= kScriptStepSkipped;
        s.stepId = 0;
        s.idTicks = 0;
        s.stepSub = 0;
        s.stepTick = 0;
        return kSequenceNextStep;
    }
    if (scriptStepAt(step).id == kScriptStepEnd) return kSequenceEnded;
    ++s.totalTicks;
    s.flags |= kScriptStepSkipped;
    return enterStep(step + uint32_t(n) * kScriptStepSize, true);
}

void scriptNextStep() {
    scriptState().clearStepPhase();
    scriptSequenceSkip(1);
}

void scriptNextStepClearingPhases() {
    ScriptState& s = scriptState();
    s.stepPhase = 0;
    s.stepSubPhase = 0;
    scriptSequenceSkip(1);
}

uint32_t scriptActorSpawnRecord(uint32_t record) {
    const uint32_t actor = spawnRecord(record);
    if (actor != 0) pointAtWord(scriptState().actors[dataRecord<SpawnRecord>(record, SpawnRecord::kBytes).listSlot], actor);
    return actor;
}

void scriptActorFreeRecord(uint32_t record) {
    ActorRef<ActorHeader>& slot = scriptState().actors[dataRecord<SpawnRecord>(record, SpawnRecord::kBytes).listSlot];
    if (slot) freeActor(*slot);
    slot = nullptr;
}

void scriptReplayPadOn() {
    ScriptState& s = scriptState();
    Player& p = player();
    s.savedInputMode = p.inputMode;
    replayPad().clear();
    p.inputMode = kInputReplay;
    if (s.id != kExitTransitionScript) p.status = 0;
}

void scriptReplayPadOff() { player().inputMode = scriptState().savedInputMode; }

void scriptPlayerReset() {
    Player& p = player();
    p.enterState(PlayerState::Playing);
    p.airborne = 0;
    p.onPlatform = 0;
    p.setPrevMoveDir(MoveDir::Forward);
    p.setMoveDir(MoveDir::Forward);
    p.velocity.clearWords();
    p.clearDirectionOffsetWords();
    p.interactionTarget.clear();
    p.carryFlags = 0;
    scriptReplayPadOn();
}

int scriptSkipRequested() {
    ScriptState& s = scriptState();
    if ((pad0().pressed & kPadStart) && !(s.flags & kScriptSkipLocked)) s.skipRequested = kSkipRequested;
    const GameState& g = gameState();
    const FadeState& f = fadeState();
    if (g.gameMode() == GameMode::Field && g.fieldStep() == FieldStep::Playing && f.busy == 0 && f.covered == 0 &&
        s.skipRequested != 0 && !(s.flags & kScriptUnskippable)) {
        s.flags |= kScriptSkipTaken;
        return 1;
    }
    return 0;
}

namespace {
// exe 0x53a5e0 / 0x51bc90: the steps' state with the face tracks; the step's routine run when it has one, or whatever
// it is (exe 0x51bc90 does not test it)
void stepsState(ScriptStepRoutines routines, bool anyRoutine) {
    ScriptState& s = scriptState();
    const ScriptStep step = scriptStepAt(s.steps.value());
    if (step.id != kScriptStepEnd) {
        const RoutineRef routine = stateRoutine(step.routine);
        if (anyRoutine || routine) routines(routine);
        if (scriptSkipRequested() == 0 || display().drawBuffer == 0) {
            scriptSequenceTick();
            faceTracksTick(s.faceTracks);
            return;
        }
        scriptFade(kScriptSkipWipe, 0);
    }
    s.enterState(CutsceneState::WrapUp);
    scriptSequenceTick();
    faceTracksTick(s.faceTracks);
}
} // namespace

void scriptStepsState(ScriptStepRoutines routines) { stepsState(routines, false); }

void scriptStepsStateAnyRoutine(ScriptStepRoutines routines) { stepsState(routines, true); }

void scriptStepsStateHeadTurn(ScriptStepRoutines routines) {
    constexpr int16_t kHeadTurnLimit = 0x300;
    ScriptState& s = scriptState();
    const ScriptStep step = scriptStepAt(s.steps.value());
    bool wrapUp = step.id == kScriptStepEnd;
    if (!wrapUp) {
        const RoutineRef routine = stateRoutine(step.routine);
        if (routine) routines(routine);
        if (scriptSkipRequested() != 0 && display().drawBuffer != 0) {
            scriptFade(kScriptSkipWipe, 0);
            wrapUp = true;
        }
    }
    if (wrapUp) s.enterState(CutsceneState::WrapUp);
    Player& p = player();
    ScriptHeadTarget& target = scriptState().part<ScriptHeadTarget>();
    const uint16_t h = uint16_t(p.leanTarget);
    int d = int((uint32_t(h) + uint16_t(p.rotation.y)) & kAngleMask) - int(uint16_t(target.yaw) & kAngleMask);
    if (uint32_t(d + kHalfTurn) > 0x1000u) d = int((uint32_t(-d) ^ 0xfffu) + 1u);
    const int16_t turn = target.step;
    const int half = int(turn) >> 1;
    if (d < -half) {
        p.leanTarget = int16_t(uint16_t(h + turn));
        if (int16_t(h + turn) > kHeadTurnLimit) p.leanTarget = kHeadTurnLimit;
    } else if (d > half) {
        p.leanTarget = int16_t(uint16_t(h - turn));
        if (int16_t(h - turn) < -kHeadTurnLimit) p.leanTarget = int16_t(-kHeadTurnLimit);
    }
    scriptSequenceTick();
    faceTracksTick(s.faceTracks);
}

void scriptStepsStateForCast(ScriptStepRoutines routines) {
    ScriptState& s = scriptState();
    scriptState().part<CastTimelineStep>().stepId = s.stepId;
    scriptState().part<CastTimelineStep>().stepSub = s.stepSub;
    scriptState().part<CastTimelineTick>().stepTick = s.stepTick;
    const ScriptStep step = scriptStepAt(s.steps.value());
    if (step.id != kScriptStepEnd) {
        const RoutineRef routine = stateRoutine(step.routine);
        if (routine) routines(routine);
        if (scriptSkipRequested() == 0 || display().drawBuffer == 0) {
            scriptSequenceTick();
            return;
        }
        scriptFade(kScriptSkipWipe, 0);
    }
    s.enterState(CutsceneState::WrapUp);
    scriptSequenceTick();
}
namespace {
// exe 0x52f420 / 0x5328b0 / 0x52fa20: the steps' state without the face tracks; the skip with the wipe (taken while a
// frame of buffer 1 is drawn) or at once without it; the step's routine run when it has one, or whatever it is (exe
// 0x52fa20 does not test it)
void stepsStateNoFaces(ScriptStepRoutines routines, bool skipWipe, bool anyRoutine) {
    ScriptState& s = scriptState();
    const ScriptStep step = scriptStepAt(s.steps.value());
    bool wrapUp = step.id == kScriptStepEnd;
    if (!wrapUp) {
        const RoutineRef routine = stateRoutine(step.routine);
        if (anyRoutine || routine) routines(routine);
        if (scriptSkipRequested() != 0 && (!skipWipe || display().drawBuffer != 0)) {
            if (skipWipe) scriptFade(kScriptSkipWipe, 0);
            wrapUp = true;
        }
    }
    if (wrapUp) s.enterState(CutsceneState::WrapUp);
    scriptSequenceTick();
}
} // namespace

void scriptStepsStateNoFaces(ScriptStepRoutines routines) { stepsStateNoFaces(routines, true, false); }

void scriptStepsStateNoFacesAnyRoutine(ScriptStepRoutines routines) { stepsStateNoFaces(routines, true, true); }

void scriptStepsStateSkipNoWipe(ScriptStepRoutines routines) { stepsStateNoFaces(routines, false, false); }

void scriptEndBlock() {
    const RoomParams params = *roomDraw().params;
    setFogColour(params.fogColour[0], params.fogColour[1], params.fogColour[2]); // exe 0x40ba90
    stage().depthOffset = params.depthOffset;
    setCoordinateShift(params.coordinateShift);
    Player& p = player();
    p.idleFaces[0] = 0;
    p.idleFaces[1] = 0;
    p.immunity = 0;
    soundSetListener(ListenerRef::playerPosition(), ListenerRef::playerRotation(), kListenerOnPlayer);
    scriptCameraEnd();
    scriptState().flags = kScriptEnded;
}

} // namespace od::port
