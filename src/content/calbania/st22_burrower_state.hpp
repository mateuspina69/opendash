// The class states of st22_burrower.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::st22_burrower_state {

struct BurrowerFields : ActorClassState {
    explicit BurrowerFields(AnimatedActor& actor) : throws(actor.params[0]), rockVariant(actor.params[3]) {}
    uint8_t& throws;      // it throws (its spawn parameter)
    uint8_t& rockVariant; // the variant of its rocks
    HitBox hitBox;        // its contact output's bytes are the fields below
    FieldAlias<uint16_t, int16_t> bits{hitBox.contact.x};            // kBurrower*
    int16_t& timer = hitBox.contact.y;
    PiecesField<uint32_t> pushResult{hitBox.contact.z, hitBox.contactKind}; // the push-out's result of the tick
    uint16_t neighbour = 0;  // the neighbour word (game/enemy_neighbours)
    int16_t groundY = 0;     // the ground it rises from / sinks into
    uint8_t lungeSlot = 0;   // the lunge effect's slot (0: none)
    uint8_t rockSlot = 0;    // the rock held (0: none)
    uint8_t rockHold = 0;    // ticks the rock stays in the hand
    uint8_t throwsLeft = 0;
    int16_t heading = 0;
    int16_t wallTurn = 0;    // ticks the wall's heading is kept

    // bits ORed into the low byte (the exe's byte read-modify-write)
    void setBitsByte(uint8_t b) { bits = uint16_t(bits | b); }

    void clearBlock() override {
        hitBox = {};
        neighbour = 0;
        groundY = 0;
        lungeSlot = 0;
        rockSlot = 0;
        rockHold = 0;
        throwsLeft = 0;
        heading = 0;
        wallTurn = 0;
    }
};

} // namespace od::port::st22_burrower_state
