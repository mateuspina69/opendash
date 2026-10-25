// The class states of fire_ruin_servbot_cast.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::fire_ruin_servbot_cast_state {

// Its own bytes: its head's turn (<< 4), its pose done by the head pose this tick, the blink's tick, its row, started
struct ServbotFields : ActorClassState {
    explicit ServbotFields(AnimatedActor&) {}
    int8_t headTurn = 0;
    uint8_t posed = 0;
    uint8_t blinkTick = 0;
    int8_t blinkRow = 0;
    uint8_t blinking = 0;

    void clearBlock() override {
        headTurn = 0;
        posed = 0;
        blinkTick = 0;
        blinkRow = 0;
        blinking = 0;
    }
};

} // namespace od::port::fire_ruin_servbot_cast_state
