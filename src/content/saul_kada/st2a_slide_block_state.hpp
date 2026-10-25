// The class states of st2a_slide_block.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::st2a_slide_block_state {

// Its own bytes: its hit box, its home offset, the rock table's index
struct BlockFields : ActorClassState {
    explicit BlockFields(StaticActor& actor) : hitBox(actor.hitBox) {}
    HitBoxHead& hitBox;
    uint16_t home = 0;
    uint8_t rockIndex = 0;

    void clearBlock() override {
        home = 0;
        rockIndex = 0;
    }
};

} // namespace od::port::st2a_slide_block_state
