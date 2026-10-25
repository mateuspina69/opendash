// The class states of st33_falling_rock.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::st33_falling_rock_state {

// Its own bytes: its hit box, a byte its start sets (its reader is not traced)
struct RockFields : ActorClassState {
    explicit RockFields(StaticActor& actor) : hitBox(actor.hitBox) {}
    HitBoxHead& hitBox;
    uint8_t startMark = 0;

    void clearBlock() override {
        startMark = 0;
    }
};

} // namespace od::port::st33_falling_rock_state
