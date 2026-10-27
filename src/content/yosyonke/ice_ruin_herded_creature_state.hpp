// The class states of ice_ruin_herded_creature.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::ice_ruin_herded_creature_state {

// The creature's own bytes
struct HerdFields : ActorClassState {
    explicit HerdFields(AnimatedActor& actor) : bits(actor.guardianBits), pensFoundTaken(actor.hopperBytes[0]), watchesPlayer(actor.townBirdHits), frontHitArmed(actor.townBirdHitsHigh) {}
    uint8_t& bits;
    uint8_t& pensFoundTaken;
    uint8_t& watchesPlayer;
    uint8_t& frontHitArmed;
    HitBox hit; // its contact motion (x: the yaw of the hit, the hit list's output) read by the front-hit test
    int16_t heading = 0;
    int8_t cooldown = 0; // counts up to 0
    union {
        uint16_t timer = 0;
        int16_t timerSigned;
    };
    uint16_t neighbour = 0; // the neighbour word (game/enemy_neighbours)
    uint16_t lastDamage = 0;

    void clearBlock() override {
        hit = {};
        heading = 0;
        cooldown = 0;
        timer = 0;
        neighbour = 0;
        lastDamage = 0;
    }
};

// The exe globals ice_ruin_herded_creature.cpp keeps (their exe addresses in the comments)
struct IceRuinHerdedCreatureGlobals {
    uint8_t initModelVariant = 0; // 0xa59e4c
    uint32_t initPlaceIndex = 0; // 0xa59e50
};
inline IceRuinHerdedCreatureGlobals& iceRuinHerdedCreatureGlobals() {
    static IceRuinHerdedCreatureGlobals s;
    return s;
}

} // namespace od::port::ice_ruin_herded_creature_state
