// The class states of smoke_puff.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::smoke_puff_state {

// Its own bytes: its hit box, its tick
struct PuffFields : ActorClassState {
    explicit PuffFields(ProjectileActor& actor) : hitBox(actor.hitBox) {}
    HitBoxHead& hitBox;
    int16_t tick = 0;
    PartField<uint8_t, int16_t> tickLow{tick, 0};
};

} // namespace od::port::smoke_puff_state
