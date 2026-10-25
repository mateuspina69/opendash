// The class states of st20_props.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::st20_props_state {

// Its own bytes: its hit box, a word, its tick, the gong's cooldown
struct PropFields : ActorClassState {
    explicit PropFields(StaticActor& actor) : hitBox(actor.hitBox) {}
    HitBoxHead& hitBox;
    uint32_t word = 0;
    uint16_t tick = 0;
    uint16_t ringCooldown = 0;

    void clearBlock() override {
        word = 0;
        tick = 0;
        ringCooldown = 0;
    }
};

} // namespace od::port::st20_props_state
