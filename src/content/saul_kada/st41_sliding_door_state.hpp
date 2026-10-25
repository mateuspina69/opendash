// The class states of st41_sliding_door.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::st41_sliding_door_state {

// Its own bytes: the slide's ticks left
struct DoorFields : ActorClassState {
    explicit DoorFields(StaticActor&) {}
    uint16_t slideTicks = 0;

    void clearBlock() override {
        slideTicks = 0;
    }
};

} // namespace od::port::st41_sliding_door_state
