// The class states of exit_transitions.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/script_state.hpp"
#include "game/state_array.hpp"

namespace od::port::exit_transitions_state {

// ---- kind 11 (exit type 13: a teleporter pad, ST40): exe 0x42cb50 --------------------------------------------------
// Transcribed from the disassembly but NOT checked by a golden: in the oracle the player stays frozen in ST40/0 (the
// room's camera mode 2 of the room handler) and never reaches the pads (trans_ST40_camera2 only covers that camera).
// The rising spark's bytes (effect type 0x1e variant 3, its class exe 0x43b7b0 in content/late_game/st40_fx; its age is
// its state), spawned here by the teleport and there by the teleport ring: its velocity (+0xc..+0xe), the byte after it,
// the short after its position, its tail (+0x18..+0x1a), its life (+0x1b) and its colour
struct TeleporterSparkFields : ActorClassState {
    explicit TeleporterSparkFields(EffectActor& actor) : velocity(actor.params, 0), pad(actor.params[3]), spare(actor.posSpare) {}
    ArrayFields<int8_t, 3, uint8_t, 4> velocity;
    uint8_t& pad;
    int16_t& spare;
    StateArray<int8_t, 3> tail{};
    uint8_t life = 0;
    StateArray<uint8_t, 3> colour{};
};

// the door kinds (0 / 1 / 2 / 6): the door segment, the segment beyond it, the passage to another stage
struct DoorScriptBytes : ScriptPart<DoorScriptBytes> {
    uint16_t doorSegment = 0; // +0x14
    uint16_t farSegment = 0; // +0x16
    uint16_t otherStage = 0; // +0x18
};

// the door actor (kind 14): its slot, the exit it stands on, the sink's speed
struct DoorActorScriptBytes : ScriptPart<DoorActorScriptBytes> {
    uint8_t doorSlot = 0; // +0xc
    uint8_t exitIndex = 0; // +0xd
    int16_t sinkSpeed = 0; // +0xe
};

// the ladder (kind 9): the climb's speed and ticks; the push (kind 10): its ticks
struct ClimbScriptBytes : ScriptPart<ClimbScriptBytes> {
    int16_t climbSpeed = 0; // +0xc
    uint32_t ticksLeft = 0; // +0x1c
    // exe 0x40dc40 over +0xc..+0x1b: the members there zeroed (past it: kept)
    void clearSetUpRun() override {
        climbSpeed = {};
    }
};

// the teleporter (kind 11): the sparks' point, the pad's side, the RNG state saved, the ticks
struct TeleportScriptBytes : ScriptPart<TeleportScriptBytes> {
    ShortVec4 sparkPoint; // +0xc
    uint8_t padSide = 0; // +0x14
    uint16_t savedRng = 0; // +0x16
    uint32_t ticks = 0; // +0x1c
    // exe 0x40dc40 over +0xc..+0x1b: the members there zeroed (past it: kept)
    void clearSetUpRun() override {
        sparkPoint = {};
        padSide = {};
        savedRng = {};
    }
};

} // namespace od::port::exit_transitions_state
