// The class states of crash_landing_cast.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::crash_landing_cast_state {

// the trailing prop's class block: its trail's clock
struct TrailingPropFields : ActorClassState {
    explicit TrailingPropFields(AnimatedActor&) {}
    uint32_t trailClock = 0;

    void clearBlock() override {
        trailClock = 0;
    }
};

} // namespace od::port::crash_landing_cast_state
