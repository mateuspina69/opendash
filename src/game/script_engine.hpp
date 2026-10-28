// The script engine: the script block (game/script_state) started and ended (exe 0x5112b0 / 0x511270 / 0x5116a0), the
// runner of the frame body (exe 0x5128b0: script 0 is the armed exit's transition, game/exit_transitions; the others
// the story's, game/story), the step sequences the scripts run (exe 0x511390 / 0x5114d0 / 0x5115c0, with their camera
// script, game/camera_script), the replay pad the player is handed to (exe 0x5117f0 / 0x511890) and the script actors
// (exe 0x511740 / 0x511760), the cutscene skip (exe 0x511790) and the generic step runners of the cutscene scripts (exe
// 0x53a5e0, 0x537c50).
#pragma once

#include <cstdint>
#include <vector>

#include "engine/data_source.hpp"

namespace od::port {

struct PlayerEnv;

// exe 0x5112b0(id): script `id` started (not while the player is dead or a script runs): the block cleared, the
// player's faces and immunity, the sound listener on the camera
void scriptStart(uint8_t id);
// exe 0x511270(id): ... with a running event ended first and the windows closed
void scriptRun(uint8_t id);
// exe 0x5128b0 (the frame body's first step): the running script's tick
void scriptRunnerTick(const PlayerEnv& env);
// exe 0x511720 (the frame body's end): the block's ended mark cleared
void scriptBlockIdle();

// What a sequence tick did (exe 0x5114d0's return)
constexpr int kSequenceStepSkipped = 3; // the tick only cleared the skip mark
constexpr int kSequenceNewId = 2;       // the next step, of another id
constexpr int kSequenceNextStep = 1;    // the next step, the same id
constexpr int kSequenceRunning = 0;     // the step runs on
constexpr int kSequenceEndless = -1;    // the step runs until moved on
constexpr int kSequenceEnded = -2;      // the list is over
constexpr int kSequencePaused = -3;     // the camera script holds the sequence

// exe 0x511390(camera script, steps): a step sequence on the script camera (game/camera_script), its first step's ids
void scriptSequenceStart(uint32_t cameraScript, uint32_t steps);

// A step list (exe data) through its typed load: its 8-byte steps {id, sub, ticks, routine} up to the kScriptStepEnd
// one, each routine a function slot, so every source holds the whole list (a cutscene's steps past a source's shorter
// copy would read untranslated routines)
struct ScriptStepList : DataRecord {
    static constexpr uint32_t kBytes = 8;
    static constexpr uint32_t kMaxSteps = 0x200; // the read's bound (a list ends at its end step)
    struct Step {
        uint8_t id = 0, sub = 0;
        int16_t ticks = 0;
        RoutineRef routine;
    };
    std::vector<Step> steps; // the end step included
    static ScriptStepList load(const TableReader& r, TableKey key);
};
// the step list of a table, as a namespace-scope handle per id (registered for the loads every table is checked by)
template <TableId Id> inline const DataTable<ScriptStepList> scriptStepList{Id};
// scriptSequenceStart with a typed step list (loaded whole first)
void scriptSequenceStart(uint32_t cameraScript, const DataTable<ScriptStepList>& steps);
// exe 0x5114d0: one tick of the step list (the camera script first unless the block runs without it): the counters
// (total, this id, this step), the next step when this one's ticks are over
int scriptSequenceTick();
// exe 0x5115c0(n): the list moved n steps ahead (the skip mark set)
int scriptSequenceSkip(int n);
// a step routine's move to the next step: its phase bytes cleared (the word store at +6), the list one step ahead ...
void scriptNextStep();
// ... the phase bytes cleared one byte at a time
void scriptNextStepClearingPhases();
// exe 0x5117f0: the player on the replay pad (its input mode saved, the pad cleared; a story script also clears its
// status) / exe 0x511890: its input mode back
void scriptReplayPadOn();
void scriptReplayPadOff();
// exe 0x511840: the player back to its play with its motion cleared (the airborne / platform bytes, the move
// directions, the velocity, the move direction's angles, the interaction target, the carry flags), then on the replay
// pad (exe 0x5117f0)
void scriptPlayerReset();
// exe 0x511740(record) / exe 0x511760(record): the script actor of a spawn record spawned into the block's actors
// [record + 1] (its address, 0 when its pool is full) / freed
uint32_t scriptActorSpawnRecord(uint32_t record);
void scriptActorFreeRecord(uint32_t record);
// exe 0x511790: start pressed (unless the block locks it) asks for the skip; the skip is taken (kScriptSkipTaken, 1)
// once the field plays, no fade runs or covers the screen and the block allows it; else 0
int scriptSkipRequested();
// the fade a taken skip starts (a wipe of type 0x20, variant 2)
constexpr uint8_t kScriptSkipWipe = 0x22;

// The port of a step's routine (each script dispatches its steps by the named routine; the pointer for a gap's message)
using ScriptStepRoutines = void (*)(const RoutineRef& routine);
// exe 0x53a5e0: the generic state 1 of the cutscene scripts: the current step's routine (run through the script's
// `routines`), the skip (taken while a frame of buffer 1 is drawn: the skip's wipe, then state 2), the sequence's tick
// and the player's face tracks; at the list's end state 2
void scriptStepsState(ScriptStepRoutines routines);
// exe 0x51bc90: the same, the step's routine run whatever it is (the exe does not test it for none)
void scriptStepsStateAnyRoutine(ScriptStepRoutines routines);
// exe 0x537c50: the same state 1 for the scripts whose cast keys on the cast's copy of the timeline (game/script_state
// CastTimeline: the step's ids and tick copied first), without the player's face tracks
void scriptStepsStateForCast(ScriptStepRoutines routines);
// exe 0x531310: scriptStepsState's state 1 (the skip ends it at once, the state 2 store after the step) with the
// player's head (its lean target) turned toward the script's head target (ScriptHeadTarget: by its step, within
// +-0x300) before the sequence's tick and the face tracks
void scriptStepsStateHeadTurn(ScriptStepRoutines routines);
// exe 0x52f420 (and its twin 0x52fa20): the same state 1 without the face tracks -- the current step's routine, the
// skip (taken while a frame of buffer 1 is drawn: the skip's wipe, then state 2), the sequence's tick; at the list's end
// state 2
void scriptStepsStateNoFaces(ScriptStepRoutines routines);
// exe 0x52fa20: the same, the step's routine run whatever it is (the exe does not test it for none)
void scriptStepsStateNoFacesAnyRoutine(ScriptStepRoutines routines);
// exe 0x5328b0: ... the skip taken at once, without the wipe
void scriptStepsStateSkipNoWipe(ScriptStepRoutines routines);

// exe 0x5116a0: the script ended: the room's fog colour, depth offset and coordinate shift back, the player's faces and
// immunity cleared, the sound listener on the player, the camera back to the follow mode, the block marked ended
void scriptEndBlock();

} // namespace od::port
