// The running stage's state that the systems read: the globals the stage / room setup writes (exe 0x5d5a80 /
// 0x5d5b30, game/spawn; the room's draw parameters, game/room_params) -- the coordinate shift, the draw depth range and
// the room's draw values, the animation bank, the town routes, the exits and the transition kind handlers.
#pragma once

#include <cstdint>

#include "engine/data_source.hpp"
#include "game/block_parts.hpp"
#include "game/loaded_data.hpp"
#include "game/state_array.hpp"

namespace od::port {

// The two spare buffers (StageState::spareBuffer picks the one in use)
constexpr uint32_t kSpareBufferA = 0x7a8800, kSpareBufferB = 0x784800;

struct StageState {
    uint16_t depthOffset = 0;   // the room record's depth offset (the camera scripts set / restore it; its reader, in
                                // the draw code, is not traced)
    uint16_t depthRange = 0;    // the draw functions' depth range (* 4: the far distance)
    uint16_t effectsFar = 0;    // the projected effects' far depth (they show up to view z = it * 4)
    uint8_t bankBuffer = 0;     // the type-1 buffer of the stage's bank (0: kBankBufferA, else kBankBufferB; a script
                                // loads its next scene's DAT into the other)
    uint8_t spareBuffer = 0;    // the spare buffer in use (0: kSpareBufferA, else kSpareBufferB: the stage texture
                                // DAT's type-2 segment, the status sub-screen's saves)
    uint32_t modelBank = 0;     // the stage's type-1 bank: model entries, model tables, animations (its address in
                                // the game state)
    uint8_t gridByte = 0;       // the room record's +6 (its reader is not traced)
    StateArray<int16_t, 4> lodDistances{}; // the room's LOD distances
    uint32_t textureAnimations = 0;    // the room's texture animations (game/room_uv_animation; cleared by the room
    uint8_t textureAnimationCount = 0; // setup)
    uint8_t coordinateShift = 0; // world units -> bone-point units (<< (shift & 0x1f))
    DataPtr<> depthCue;         // the models' depth cue by their depth >> 7 (u32 each, exe data; game/model_colour; the
                                // title points it at the exe's table)
    uint16_t drawWord = 0;      // the room record's draw bytes (their readers, in the draw code, are not traced)
    uint8_t drawByteA = 0;
    uint8_t drawByteB = 0;
    int16_t warpOffsetX = 0;    // the in-stage warps' offsets subtracted (x / z; their reader is not traced)
    int16_t warpOffsetZ = 0;
    uint32_t townRoutes = 0;    // the town NPCs' route table (game/town_npc; only stages with walkers)
    uint32_t exitTable = 0;     // the room's exits (game/transition_state ExitEntry, 0x18 bytes each)
    DataPtr<> transitionHandlers; // the stage's transition kind handlers (exe data the stage setup points: a routine
                                  // pointer per transition kind, which plays the kind's states; game/exit_transitions)
    RoutineId roomCallback = RoutineId::None;  // the minimap's callback the stage / room setups set (exe 0xa72584:
                                               // the draw pass runs it, game/minimap; the room setup switches the
                                               // minimap on while it is set)
    RoutineId script8Callback = RoutineId::None;  // the routine script 8 runs (the shop stages: the shop menu
                                                  // 0x5d1e30; None: none)
    RoutineId script30Callback = RoutineId::None; // the routine script 0x30 runs (ST04: Roll's development room
                                                  // 0x545e60, ST19: 0x548650; None: none)
    RoutineId secondRoomCallback = RoutineId::None; // the map page's callback the stage setups set (exe 0xa72230:
                                                    // the status sub-screen's map page exe 0x5e86a0 runs it)
    uint32_t clutAnimations = 0; // the stage's CLUT animations (8-byte entries {sequence, CLUT base})
    uint32_t groundKinds = 0;    // the stage's ground kinds by cell attribute (0: none)
    RoutineId vertexColourHook = RoutineId::None; // the strip renderers' vertex-colour routine (the room setup: the
                                                  // exe's empty routine; ST27 / ST34: the lava glow; only render
                                                  // families 2 / 3 call it)
};
// the routine the town rooms' room functions point roomCallback at (ST08 / ST0B / ST20 sub 0, ST2A / ST2B subs 0..1;
// its body is not ported)
constexpr RoutineId kTownRoomCallback = RoutineId::TownRoomCallback;

inline StageState& stage() {
    static StageState s;
    return s;
}

// A part of the stage's own block (0x40 bytes; game/block_parts): each stage's scripts and classes name the bytes they
// keep there in parts of their own (a struct deriving StagePart<itself>; the offsets in the comments are the exe's,
// from the block's start): the fire mission's, the guardian's, the licence exam's clock, the quiz's ... Made zero on
// the first ask, dropped at the next stage's setup.
struct StageBlockPart {
    virtual ~StageBlockPart() = default;
    // its members zeroed (the block cleared)
    virtual void clear() = 0;
};
template <class T> struct StagePart : StageBlockPart {
    void clear() override { static_cast<T&>(*this) = T{}; }
};
// the clears of the block (BlockPartsObserver::bytesCleared): the whole block (+0x00..+0x3f)
enum class StageBlockClear : unsigned { All = 0 };

// The block's first word (+0x00): the stage setup writes it from the stage's table; the stages that keep it name it
// (the exam's clock, the scene's saved RNG, the boss music's latch)
struct StageBlockHead : StagePart<StageBlockHead> {
    uint32_t word = 0; // +0x00
};

struct StageBlock {
    BlockParts<StageBlockPart> parts;
    uint32_t unwritten28 = 0; // +0x28: no stage keeps a byte there (only the block's clears write it; the rider walk
                              // past the static pool reads it, game/carry)
    // the stage's part T of the block (made zero on its first ask)
    template <class T> T& part() { return parts.part<T>(); }
    // a new stage's setup: the previous stage's parts dropped (the exe keeps the bytes; no stage reads them)
    void newStage() { parts.drop(); }
    // exe 0x40dc40 over the block: the parts' members zeroed
    void clearAll() {
        unwritten28 = 0;
        parts.bytesCleared(unsigned(StageBlockClear::All));
        parts.forEach([](StageBlockPart& p) { p.clear(); });
    }
};
inline StageBlock& stageBlock() {
    static StageBlock s;
    return s;
}

} // namespace od::port
