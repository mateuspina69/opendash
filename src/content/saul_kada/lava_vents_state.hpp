// The class states of lava_vents.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::lava_vents_state {

// The lava floor's own bytes: the ticks to the next plume
struct VentsFields : ActorClassState {
    explicit VentsFields(ControllerActor&) {}
    uint16_t plumeTicks = 0;
};

} // namespace od::port::lava_vents_state
