// The script block's state (game/script_engine runs it; the scripts are game/exit_transitions' script 0 and the
// story's, dispatched by game/story): the block 0xa725a0 -- its flags (a script runs / just ended, the camera script
// paused, the skip), the running script's id, the step sequence's ids, counters and step pointer, the script's state
// machine, the player's saved input mode, the script's own bytes, its actors, the player's face tracks -- as the
// scripts and the systems that check for a running script see it.
#pragma once

#include <cstddef>
#include <cstdint>

#include "game/actor_state.hpp"
#include "game/block_parts.hpp"
#include "game/loaded_data.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/state_array.hpp"

namespace od::port {

constexpr uint32_t kScriptBlock = 0xa725a0;
constexpr uint32_t kScriptBlockSize = 0xc0;
// the exe address of the script's own bytes (+0xc), a value some scripts keep for their cast (never read through)
constexpr uint32_t kScriptBytesAddress = kScriptBlock + 0xc;

// +0: the block's flags
constexpr uint8_t kScriptRunning = 0x01;      // a script runs (the runner ticks it)
constexpr uint8_t kScriptNoCameraScript = 0x02; // the step sequence runs without its camera script
constexpr uint8_t kScriptUnskippable = 0x04;  // a requested skip is not taken
constexpr uint8_t kScriptStepSkipped = 0x08;  // the step list was moved on (the next sequence tick only clears this)
constexpr uint8_t kScriptSkipTaken = 0x10;    // the cutscene's skip was taken
constexpr uint8_t kScriptSkipLocked = 0x20;   // start does not ask for the skip
constexpr uint8_t kScriptEnded = 0x40;        // the script ended this frame (cleared at the frame's end; the player's
                                              // input stays off for that frame)
constexpr uint8_t kScriptCameraPaused = 0x80; // the camera script and the step list are held

// One step of a step sequence (exe data, 8 bytes; a list ends at id kScriptStepEnd)
struct ScriptStep : DataRecord {
    static constexpr uint32_t kBytes = 8;
    uint8_t id = 0;
    uint8_t sub = 0;
    int16_t ticks = 0;    // the step's length (kScriptStepEndless: until moved on)
    uint32_t routine = 0; // the exe routine the transitions run each tick of the step (0: none)
    template <class R> static ScriptStep load(const R& r) { return {{}, r.u8(0), r.u8(1), r.s16(2), r.u32(4)}; }
};
// the step at a step pointer
inline ScriptStep scriptStepAt(uint32_t pointer) { return dataRecord<ScriptStep>(pointer, ScriptStep::kBytes); }
constexpr uint8_t kScriptStepEnd = 0xff;
constexpr int16_t kScriptStepEndless = -1;
constexpr uint32_t kScriptStepSize = 8;

// +4 of the cutscene scripts: the states most of them share (each script's own table names the routines)
enum class CutsceneState : uint8_t {
    SetUp = 0,  // the camera script and the steps, the replay pad, the player taken over
    Steps = 1,  // the step runner (game/script_engine scriptStepsState, or the script's own)
    WrapUp = 2, // the request of the next room / stage, the player back, the block's end
};

// A part of the running script's own bytes (+0xc..+0x1f of the block; game/block_parts): each script names the bytes
// it keeps in parts of its own (a struct deriving ScriptPart<itself>; the offsets in the comments are the exe's), made
// zero on its first ask and dropped when the script ends (a new one starts, the block is cleared)
struct ScriptBytesPart {
    virtual ~ScriptBytesPart() = default;
    // exe 0x40dc40 over +0xc..+0x1b (a set-up's clear): the part's members there zeroed (one in the last word kept)
    virtual void clearSetUpRun() = 0;
};
template <class T> struct ScriptPart : ScriptBytesPart {
    // (a part whose members are all in +0xc..+0x1b; a part with a member in the last word overrides it)
    void clearSetUpRun() override { static_cast<T&>(*this) = T{}; }
};
// the clears of the script's bytes (BlockPartsObserver::bytesCleared): all of them (+0xc..+0x1f), the set-up's run
// (+0xc..+0x1b), the stage load's first and last words (+0xc..+0xf, +0x1c..+0x1f)
enum class ScriptBytesClear : unsigned { All = 0, SetUpRun = 1, StageLoadWords = 2 };

// The cast's copy of the timeline (the runner exe 0x537c50 and the scripts' own copies make it every tick for their
// cast; the cutscene classes key on it): the step's tick (+0xc) and, apart (a script that copies the tick only keeps
// bytes of its own at +0x10), its id and sub (+0x10 / +0x11)
struct CastTimelineTick : ScriptPart<CastTimelineTick> {
    int32_t stepTick = 0; // +0xc
};
struct CastTimelineStep : ScriptPart<CastTimelineStep> {
    uint8_t stepId = 0;  // +0x10
    uint8_t stepSub = 0; // +0x11
};
// The cast's start (+0xc): the scripts whose cast waits for it set it (the voice played, the opening ended)
struct ScriptCastStart : ScriptPart<ScriptCastStart> {
    uint8_t started = 0; // +0xc
};
// The head target of the scripts whose state 1 is exe 0x531310 (game/script_engine scriptStepsStateHeadTurn): the yaw
// the player's head turns toward (+0x10) and its turn a tick (+0x14)
struct ScriptHeadTarget : ScriptPart<ScriptHeadTarget> {
    int16_t yaw = 0;  // +0x10
    int16_t step = 0; // +0x14
};

// The block's native state: the head, the running script's own bytes (its parts), the step sequence's ticks and step,
// the actors (the travel map keeps bytes of its own in the records it does not use) and the player's face tracks
// (game/keyframe_tracks, shared with the cutscene actors)
struct ScriptState {
    uint8_t flags = 0;          // kScript* (0: no script, none just ended)
    uint8_t id = 0;             // the running script (0: the transition of the armed exit)
    uint8_t stepId = 0;         // the current step's id / sub (copies of its record)
    uint8_t stepSub = 0;
    uint8_t state = 0;          // the script's state machine
    uint8_t subState = 0;
    uint8_t stepPhase = 0;      // a step's own phase byte ...
    uint8_t stepSubPhase = 0;   // ... and its second one (script 0xd keeps its outcome there)
    uint8_t savedInputMode = 0; // the player's input mode before the replay pad
    uint8_t skipRequested = 0;  // start was pressed during the cutscene
    StateArray<uint8_t, 2> inputSpare{}; // the bytes the saved input's dword store clears with it (no reader)
    BlockParts<ScriptBytesPart> scriptBytes; // the running script's own bytes (its parts)
    int32_t totalTicks = 0;     // the sequence's ticks ...
    int32_t idTicks = 0;        // ... in the current step id ...
    int32_t stepTick = 0;       // ... in the current step (the script's timeline the cast keys on)
    StateArray<ActorRef<ActorHeader>, 32> actors{}; // the script's actors (any pool; null: none), spawned by the camera
                                                    // script's ops 0x40 / 0x41 into [record + 1]
    DataPtr<ScriptStep> steps; // the current step of the sequence (exe data)
    KeyframeTracks faceTracks;  // the player's face tracks (the eyes and the mouth; game/keyframe_tracks)

    bool inUse() const { return flags != 0; }
    // the running script's part T of its own bytes (made zero on its first ask)
    template <class T> T& part() { return scriptBytes.part<T>(); }
    // the state s at sub-state 0, the step phase bytes cleared (the exe's dword store at +4)
    void enterState(uint8_t s) { enterStateBytes(s); }
    void enterState(CutsceneState s) { enterState(uint8_t(s)); }
    CutsceneState cutsceneState() const { return CutsceneState(state); }
    // the state s with its sub-state and the step phase bytes cleared (the exe's four byte stores at +4 .. +7)
    void enterStateBytes(uint8_t s) {
        state = s;
        subState = 0;
        stepPhase = 0;
        stepSubPhase = 0;
    }
    // ... the step phase bytes kept (the exe's two byte stores at +4 / +5)
    void enterStateKeepingPhases(uint8_t s) {
        state = s;
        subState = 0;
    }
    // the step phase and the second phase byte cleared one byte at a time (the exe's byte stores at +6 / +7)
    void clearStepPhaseBytes() {
        stepPhase = 0;
        stepSubPhase = 0;
    }
    // the step phase and the second phase byte cleared (the exe's word store at +6)
    void clearStepPhase() { clearStepPhaseBytes(); }
    // the step phase p with the second phase byte cleared (the exe's word store at +6)
    void enterStepPhase(uint8_t p) {
        stepPhase = p;
        stepSubPhase = 0;
    }
    // the saved input mode, the skip request and +0xa / +0xb cleared (the exe's dword store at +8)
    void clearSavedInputWord() {
        savedInputMode = 0;
        skipRequested = 0;
        inputSpare.fill(0);
    }
    // the script's first 16 bytes (+0xc..+0x1b) cleared one byte at a time (a set-up's exe 0x40dc40(+0xc, +0x18))
    void clearScriptByteRun() {
        scriptBytes.bytesCleared(unsigned(ScriptBytesClear::SetUpRun));
        scriptBytes.forEach([](ScriptBytesPart& p) { p.clearSetUpRun(); });
    }
    // the script's own bytes cleared as a start does (+0xc..+0x1b one byte at a time, +0x1c as a dword): its parts
    // dropped (the new script starts at zero)
    void clearScriptBytes() { scriptBytes.dropAll(unsigned(ScriptBytesClear::All)); }
    // exe 0x5127a0's block part (a stage load): the head (flags, id, step ids), the state, the saved input and the
    // first and last words of the script's bytes cleared (dword stores)
    void clearForStageLoad() {
        flags = 0;
        id = 0;
        stepId = 0;
        stepSub = 0;
        enterStateBytes(0);
        clearSavedInputWord();
        // the script's first and last words (the script ended: its parts dropped; the exe keeps the bytes between)
        scriptBytes.dropAll(unsigned(ScriptBytesClear::StageLoadWords));
    }
    // the step tick copied into the cast's timeline (the cast runners' dword store at +0xc)
    void setStepTickCopy() { part<CastTimelineTick>().stepTick = stepTick; }
    // the face tracks cleared (a start's four dword stores)
    void clearFaceTracks() { faceTracks.clearWords(); }
    // the head's four bytes (the flags, the id, the step's id / sub) as one dword (the host's trace)
    uint32_t headBytesWord() const {
        return uint32_t(flags) | uint32_t(id) << 8 | uint32_t(stepId) << 16 | uint32_t(stepSub) << 24;
    }
    // the whole block cleared (a host's quiet game state: no script)
    void clearBlockBytes() {
        flags = id = stepId = stepSub = 0;
        enterStateBytes(0);
        clearSavedInputWord();
        scriptBytes.dropAll(unsigned(ScriptBytesClear::All));
        totalTicks = idTicks = stepTick = 0;
        actors = {};
        steps = nullptr;
        faceTracks.clearWords();
    }
};
inline ScriptState& scriptState() {
    static ScriptState s;
    return s;
}

} // namespace od::port
