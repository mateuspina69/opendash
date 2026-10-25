// The class states of ice_ruin_shrink_block.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::ice_ruin_shrink_block_state {

// Its own bytes: its hit box, the wait's ticks; its size while shrinking (the velocity's spare lane)
struct ShrinkFields : ActorClassState {
    explicit ShrinkFields(AnimatedActor& actor) : size(actor.velocity.spare) {}
    int16_t& size;
    HitBoxHead hitBox;
    uint16_t waitTicks = 0;

    void clearBlock() override {
        hitBox = {};
        waitTicks = 0;
    }
};

} // namespace od::port::ice_ruin_shrink_block_state
