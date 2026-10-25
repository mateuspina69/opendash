// The class states of sky_arc.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/state_array.hpp"

namespace od::port::sky_arc_state {

// The class's bytes past the controller head: the sky point (integer world units)
struct SkyArcFields : ActorClassState {
    explicit SkyArcFields(ControllerActor&) {}
    StateArray<int32_t, 3> point{};
};

} // namespace od::port::sky_arc_state
