// The class states of fleeing_reaverbot.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::fleeing_reaverbot_state {

// The class block: the hit box, the turn countdown, the leave request (tested, never set here), the home height, the
// neighbour word, the contact bits of the tick, the mode, the key a new animation starts at
struct ReaverbotFields : ActorClassState {
    explicit ReaverbotFields(AnimatedActor&) {}
    HitBoxHead hit;
    uint16_t ticks = 0;
    uint16_t leave = 0;
    int16_t homeY = 0;
    uint16_t neighbour = 0;
    uint8_t contact = 0; // kTouched*
    uint8_t mode = 0; // kReaverbot*
    uint8_t startKey = 0;

    void clearBlock() override {
        hit = {};
        ticks = 0;
        leave = 0;
        homeY = 0;
        neighbour = 0;
        contact = 0;
        mode = 0;
        startKey = 0;
    }
};

} // namespace od::port::fleeing_reaverbot_state
