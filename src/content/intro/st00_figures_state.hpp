// The class states of st00_figures.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::st00_figures_state {

// variant 0's bytes: the blink clock and cell (spawn bytes 8 / 9), the head's turn toward the player
struct WatchingFigureFields : ActorClassState {
    explicit WatchingFigureFields(AnimatedActor& actor) : blinkClock(actor.params[0]), blinkCell(actor.params[1]) {}
    uint8_t& blinkClock;
    uint8_t& blinkCell;
    int16_t headYaw = 0;
    int16_t headPitch = 0;

    void clearBlock() override {
        headYaw = 0;
        headPitch = 0;
    }
};

// variant 3's: its hit box
struct TargetFigureFields : ActorClassState {
    explicit TargetFigureFields(AnimatedActor&) {}
    HitBoxHead hitBox;

    void clearBlock() override {
        hitBox = {};
    }
};

} // namespace od::port::st00_figures_state
