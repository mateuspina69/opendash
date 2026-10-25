// The class states of reaverbot_walker.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::reaverbot_walker_state {

// The walker's own fields: its spawn parameters, the home point it keeps in its spin's z / spare lanes and its
// rotation's spare lane, and its class block
struct WalkerFields : ActorClassState {
    explicit WalkerFields(AnimatedActor& actor) : patrolCells(actor.params[1]), wakeFlag(actor.params, 2), homeYaw(actor.rotation.spare), homeZ(actor.spin.spare), behaviour(actor.shownParts), cooldown(actor.hopperBytes[4]), tintLow_(actor.tint, 0), tintHigh_(actor.tint, 1) {}
    FieldAlias<int8_t, uint8_t> patrolCells; // the patrol's leg (in its cell counts); non-zero: the hopper behaviour
    BytesField<int16_t, 4> wakeFlag; // the story flag it waits for (-1: never)
    int16_t& homeYaw; // the patrol's heading (its rotation's spare lane)
    int16_t& homeZ; // the home point's z (its spin's spare lane; the x in the spin's z)
    uint8_t& behaviour; // the next sub-state's row [0x88ec38]
    int8_t bits = 0; // kWalker* behaviour bits
    int8_t hitsLeft = 0; // the flinches before a knock-back
    uint8_t& cooldown; // ticks before the next lunge / shot
    int8_t group = 0; // its enemy-group entry (exe 0x5e6cd0)
    PartField<uint8_t, uint16_t> tintLow_;
    PartField<uint8_t, uint16_t> tintHigh_;
    HitBox hitBox; // its contact.x: the hit's direction
    int16_t timer = 0;
    int16_t heading = 0; // the knock's direction / the patrol's leg count / the wall turn's rate
    uint8_t contact = 0; // kWalker* contact bits of this tick
    uint16_t neighbour = 0; // game/enemy_neighbours
    void setTintBytes(uint8_t low, uint8_t high) {
        tintLow_ = low;
        tintHigh_ = high;
    }

    void clearBlock() override {
        bits = 0;
        hitsLeft = 0;
        group = 0;
        hitBox = {};
        timer = 0;
        heading = 0;
        contact = 0;
        neighbour = 0;
    }
};

} // namespace od::port::reaverbot_walker_state
