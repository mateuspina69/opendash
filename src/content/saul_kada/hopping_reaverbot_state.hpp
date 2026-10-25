// The class states of hopping_reaverbot.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::hopping_reaverbot_state {

// The Reaverbot's own bytes: its hit box, its target (the player or a partner), a timer (the wanderers' home z), the
// heading it circles / gives way along (the wanderers' home x), the stun damage taken, the neighbour word, the spin of
// its spinning animation, the wall bumps, the collision result and its flags
struct HopFields : ActorClassState {
    explicit HopFields(AnimatedActor& actor) : bumpTicks(actor.hopperBytes[0]), contact(actor.hopperBytes[2]), flags(actor.hopperBytes[3]), animKey(actor.hopperBytes[4]) {}
    uint8_t& bumpTicks;
    int8_t bumps = 0;
    uint8_t& contact; // kContact*
    uint8_t& flags; // kHop*
    uint8_t& animKey;
    HitBoxHead hitBox;
    ActorRef<Actor> target;
    union {
        uint16_t timer = 0;
        uint16_t homeZ;
    };
    union {
        int16_t heading = 0;
        uint16_t homeX;
    };
    int16_t stunDamage = 0;
    uint16_t neighbour = 0;
    uint16_t spin = 0;

    void clearBlock() override {
        bumps = 0;
        hitBox = {};
        target = nullptr;
        timer = 0;
        heading = 0;
        stunDamage = 0;
        neighbour = 0;
        spin = 0;
    }
};

} // namespace od::port::hopping_reaverbot_state
