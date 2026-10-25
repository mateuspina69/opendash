// The class states of race_bonus.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/stage_state.hpp"

namespace od::port::race_bonus_state {

// The bonus's own bytes: its rise off the player's head, its tick (the bob's phase, then the ticks left), its rest
// height (16.16)
struct BonusFields : ActorClassState {
    explicit BonusFields(StaticActor&) {}
    uint16_t rise = 0;
    int16_t tick = 0;
    int32_t restY = 0;

    void clearBlock() override {
        rise = 0;
        tick = 0;
        restY = 0;
    }
};

// The race's clock in the stage's block: the ticks it stays frozen
struct RaceClock : StagePart<RaceClock> {
    uint16_t frozenTicks = 0; // +0x12
};

} // namespace od::port::race_bonus_state
