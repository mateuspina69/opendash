// The class states of ice_ruin_critters.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::ice_ruin_critters_state {

// The critter's own bytes
struct CritterFields : ActorClassState {
    explicit CritterFields(AnimatedActor& actor) : bits(actor.shownParts), animKey(actor.hopperBytes[0]), hits(actor.hopperBytes[1]) {}
    uint8_t& bits;
    uint8_t& animKey;
    uint8_t& hits;
    HitBox hit; // its contact motion (x: the yaw of the hit, the hit list's output) turns the critter
    uint16_t timer = 0;
    int16_t heading = 0;
    uint8_t coin = 0;
    int16_t hitsInAll = 0;
    uint16_t hurtCooldown = 0;
    int16_t spawnX = 0;
    int16_t spawnZ = 0;

    void clearBlock() override {
        hit = {};
        timer = 0;
        heading = 0;
        coin = 0;
        hitsInAll = 0;
        hurtCooldown = 0;
        spawnX = 0;
        spawnZ = 0;
    }
};

} // namespace od::port::ice_ruin_critters_state
