// The class states of ruin_blocks.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::ruin_blocks_state {

// Its own fields: a piece's floor (its acceleration's y lane), the timer (its acceleration's spare lane), the hit box
// and the object-state group (variant 3) of its class block
struct BlockFields : ActorClassState {
    explicit BlockFields(StaticActor& actor) : floorY(actor.acceleration.y), timer(actor.acceleration.spare), hitBox(actor.hitBox) {}
    int16_t& floorY;
    int16_t& timer;
    HitBoxHead& hitBox;
    int32_t group = 0;
    StateArray<uint8_t, 4> clearedPad0{}; // bytes the block clear covers (no reader)
    void clearClassWordsBytes() {
        group = 0;
        clearedPad0 = {};
        hitBox = {};
    }

    void clearBlock() override {
        group = 0;
        clearedPad0 = {};
    }
};

} // namespace od::port::ruin_blocks_state
