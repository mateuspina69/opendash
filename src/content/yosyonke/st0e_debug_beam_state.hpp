// The class states of st0e_debug_beam.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/vectors.hpp"

namespace od::port::st0e_debug_beam_state {

// the beam's bytes past the mesh layout (its length in +0xc, over the spawn parameters)
struct DebugBeamFields : ActorClassState {
    explicit DebugBeamFields(ProjectileActor& actor) : length(actor.params, 0) {}
    BytesField<int32_t, 4> length;
    int32_t bodyLength = 0; // -length (the body's span, set by the rings)
    ShortVec3 lastPoint; // the beam's far end (the rings' second point, the body's last)
    int8_t width = 0;
};

} // namespace od::port::st0e_debug_beam_state
