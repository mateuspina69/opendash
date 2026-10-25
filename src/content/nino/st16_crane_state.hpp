// The class states of st16_crane.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "content/nino/st16_crane.hpp"
#include "game/field_alias.hpp"

namespace od::port::st16_crane_state {

// The crane's parameters (st16_crane.hpp) and class block
// The crane's parameters (st16_crane.hpp) and class block
struct CraneFields : ActorClassState, CraneParams {
    explicit CraneFields(AnimatedActor& actor) : CraneParams(actor), homeYaw(actor.rotation.spare) {}
    int16_t& homeYaw;  // the rotation's spare lane: its yaw when parked
    HitBox hitBox;     // its hit entry's box (the contact output after the head)
    uint8_t box = 0;   // the hung box's slot (0: none)
    int16_t turnLeft = 0; // the turn round's angle left

    void clearBlock() override {
        hitBox = {};
        box = 0;
        turnLeft = 0;
    }
};

} // namespace od::port::st16_crane_state
