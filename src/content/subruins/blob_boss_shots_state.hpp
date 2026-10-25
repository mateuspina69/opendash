// The class states of blob_boss_shots.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::blob_boss_shots_state {

// Its own bytes: its hit box, its ticks left
struct FallingShotFields : ActorClassState {
    explicit FallingShotFields(ProjectileActor& actor) : hit(actor.hitBox) {}
    HitBoxHead& hit;
    int16_t ticks = 0;
};

// Its own bytes: its hit box, its ticks left, its landed bit
struct LobbedBlobFields : ActorClassState {
    explicit LobbedBlobFields(ProjectileActor& actor) : hit(actor.hitBox), ticks(actor.debrisFlags) {}
    HitBoxHead& hit;
    uint32_t& ticks;
    uint32_t landed = 0; // bit 0
};

} // namespace od::port::blob_boss_shots_state
