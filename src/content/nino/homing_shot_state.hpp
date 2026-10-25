// The class states of homing_shot.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::homing_shot_state {

// The shot's class block: its hit box and the billboard's spin
struct ShotFields : ActorClassState {
    explicit ShotFields(ProjectileActor& actor) : hit(actor.hitBox) {}
    HitBoxHead& hit;
    int16_t spin = 0;
};

} // namespace od::port::homing_shot_state
