// The class states of st28_gunner_beam.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::st28_gunner_beam_state {

// The beam's own bytes: its hit box, its effect's ticks, the ray's dots' phase
struct BeamOwnFields : ActorClassState {
    explicit BeamOwnFields(ProjectileActor&) {}
    HitBoxHead hitBox;
    uint16_t effectTicks = 0;
    uint8_t dotPhase = 0;
};

} // namespace od::port::st28_gunner_beam_state
