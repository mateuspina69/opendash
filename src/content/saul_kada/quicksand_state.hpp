// The class states of quicksand.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/vectors.hpp"

namespace od::port::quicksand_state {

// The pit's parameters: the pull's strength, its radius; its point (the spawn record's)
struct PitParams : ActorClassState {
    explicit PitParams(ControllerActor& actor) : pullStrength(actor.params, 0), radius(actor.params, 2) {}
    BytesField<uint16_t, 4> pullStrength;
    BytesField<uint16_t, 4> radius;
    ShortVec3 point;
};

} // namespace od::port::quicksand_state
