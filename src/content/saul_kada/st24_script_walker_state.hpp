// The class states of st24_script_walker.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::st24_script_walker_state {

// The walker's own bytes: the walk-in's ticks, its pose held
struct WalkerFields : ActorClassState {
    explicit WalkerFields(AnimatedActor&) {}
    uint16_t walkTicks = 0;
    uint8_t holdPose = 0;

    void clearBlock() override {
        walkTicks = 0;
        holdPose = 0;
    }
};

} // namespace od::port::st24_script_walker_state
