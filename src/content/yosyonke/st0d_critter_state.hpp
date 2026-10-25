// The class states of st0d_critter.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::st0d_critter_state {

// the critter's bytes: its hit-list box (its contact: the angle of the hit), its timer, the hit / flee heading and the
// walk heading, the collision and behaviour bits
struct CritterFields : ActorClassState {
    explicit CritterFields(AnimatedActor& actor) : hits(actor.hopperBytes[0]), behaviour(actor.hopperBytes[1]) {}
    uint8_t& hits; // kWalk*
    uint8_t& behaviour; // kCritter* (the walls reflect kWalkerBounces)
    HitBox box;
    uint16_t timer = 0;
    int16_t turn = 0; // the hit's heading / the wall's turn of the walk
    int16_t heading = 0; // the heading it walks / flees along

    void clearBlock() override {
        box = {};
        timer = 0;
        turn = 0;
        heading = 0;
    }
};

} // namespace od::port::st0d_critter_state
