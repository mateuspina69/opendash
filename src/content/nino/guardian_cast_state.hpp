// The class states of guardian_cast.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::guardian_cast_state {

// The cast's class block: the ticks, the sparkles' count, the bone sparks' drift and count, the bone they come from
struct CastFields : ActorClassState {
    explicit CastFields(AnimatedActor&) {}
    int16_t ticks = 0;
    int16_t sparkles = 0;
    uint8_t sparkDrift = 0;
    uint8_t sparkCount = 0;
    int16_t sparkBone = 0;

    void clearBlock() override {
        ticks = 0;
        sparkles = 0;
        sparkDrift = 0;
        sparkCount = 0;
        sparkBone = 0;
    }
};

} // namespace od::port::guardian_cast_state
