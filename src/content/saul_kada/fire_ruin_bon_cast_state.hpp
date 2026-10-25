// The class states of fire_ruin_bon_cast.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::fire_ruin_bon_cast_state {

// Bon's own bytes: a tick counter (both classes' binds clear it)
struct BonCastFields : ActorClassState {
    explicit BonCastFields(AnimatedActor&) {}
    uint32_t ticks = 0;

    void clearBlock() override {
        ticks = 0;
    }
};

} // namespace od::port::fire_ruin_bon_cast_state
