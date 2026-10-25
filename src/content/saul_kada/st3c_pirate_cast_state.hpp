// The class states of st3c_pirate_cast.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::st3c_pirate_cast_state {

// The pirate's own bytes, written by its first scene (their reader is not traced)
struct PirateFields : ActorClassState {
    explicit PirateFields(AnimatedActor&) {}
    uint16_t sceneWord = 0;
    uint8_t sceneFlag = 0;
    uint8_t sceneByte = 0;

    void clearBlock() override {
        sceneWord = 0;
        sceneFlag = 0;
        sceneByte = 0;
    }
};

} // namespace od::port::st3c_pirate_cast_state
