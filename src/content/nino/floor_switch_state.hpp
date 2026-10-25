// The class states of floor_switch.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::floor_switch_state {

// its home height
struct SwitchFields : ActorClassState {
    explicit SwitchFields(StaticActor&) {}
    int16_t homeY = 0;

    void clearBlock() override {
        homeY = 0;
    }
};

} // namespace od::port::floor_switch_state
