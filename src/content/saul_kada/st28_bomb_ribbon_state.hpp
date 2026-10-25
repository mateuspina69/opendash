// The class states of st28_bomb_ribbon.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::st28_bomb_ribbon_state {

// The ribbon's bytes: its grey, its frame, its two bombs
struct RibbonFields : ActorClassState {
    explicit RibbonFields(EffectActor& actor) : grey(actor.params[0]), frame(actor.params[1]) {}
    uint8_t& grey;
    uint8_t& frame;
    ActorRef<ProjectileActor> firstBomb;
    ActorRef<ProjectileActor> secondBomb;
};

} // namespace od::port::st28_bomb_ribbon_state
