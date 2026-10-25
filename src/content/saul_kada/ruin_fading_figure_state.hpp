// The class states of ruin_fading_figure.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::ruin_fading_figure_state {

// The figure's own bytes: the fade's timer, the wait, fading, its colour's three 5-bit components
struct FigureFields : ActorClassState {
    explicit FigureFields(AnimatedActor&) {}
    uint16_t fadeTimer = 0;
    uint16_t wait = 0;
    uint8_t fading = 0;
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;

    void clearBlock() override {
        fadeTimer = 0;
        wait = 0;
        fading = 0;
        red = 0;
        green = 0;
        blue = 0;
    }
};

} // namespace od::port::ruin_fading_figure_state
