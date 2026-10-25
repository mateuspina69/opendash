// The class states of st18_props.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/vectors.hpp"
#include "game/field_alias.hpp"

namespace od::port::st18_props_state {

// The sun's fields: its point (the spawn record's) and its flare chain
struct SunFields : ActorClassState {
    explicit SunFields(ControllerActor&) {}
    ShortVec3 point;
    uint32_t flareChain = 0; // its variant's chain (exe data; the draw reads the table instead)
};

} // namespace od::port::st18_props_state
