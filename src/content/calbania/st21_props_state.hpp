// The class states of st21_props.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::st21_props_state {

// ==== the rising model (exe 0x5957d0, ST25) ===========================================================================
struct RisingModelFields : ActorClassState {
    explicit RisingModelFields(StaticActor& actor) : stop(actor.params[0]), riseBias(actor.params[3]), bobPhase(actor.hitBox.previousX), dustTicks(actor.hitBox.previousZ) {}
    uint8_t& stop; // the script's cue: stop / sink at the next bob's top
    FieldAlias<int8_t, uint8_t> riseBias; // the rise's speed is 0x100 - it
    FieldAlias<uint16_t, int16_t> bobPhase; // 0..0x7f
    int16_t& dustTicks;
    int16_t restY = 0; // where it bobs (the floor - 0x13a)
    int16_t startY = 0; // where the rise slows (the floor - 0x27a)

    void clearBlock() override {
        restY = 0;
        startY = 0;
    }
};

constexpr uint8_t kPieceThrown = 0x10; // thrown by the player: a hit entry instead of the lure

// ==== the lure piece (exe 0x596e60, ST21 / ST22) =====================================================================
struct LurePieceFields : ActorClassState {
    explicit LurePieceFields(StaticActor& actor) : owner(actor.params[0]), liftBlock(actor.params[1]), life(actor.params, 2), lifeBytes(actor.params, 2), hitBox(actor.hitBox) {}
    FieldAlias<int8_t, uint8_t> owner; // the slot of the actor whose registered actors it lures
    FieldAlias<int8_t, uint8_t> liftBlock; // no lift interaction this tick (also the pop's speed bias)
    BytesField<int16_t, 4> life;
    ArrayFields<uint8_t, 2, uint8_t, 4> lifeBytes;
    HitBoxHead& hitBox;
    uint8_t pieceFlags = 0; // kPieceThrown

    void clearBlock() override {
        pieceFlags = 0;
    }
};

} // namespace od::port::st21_props_state
