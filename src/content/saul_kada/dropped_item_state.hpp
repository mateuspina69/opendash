// The class states of dropped_item.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::dropped_item_state {

// Its own bytes: the toss's contacts (kTossWall | kTossFloor), the bob's phase, its rest height, the settle's ticks
struct ItemFields : ActorClassState {
    explicit ItemFields(StaticActor&) {}
    uint8_t contacts = 0;
    uint8_t bobPhase = 0;
    int16_t restY = 0;
    uint8_t settleTicks = 0;

    void clearBlock() override {
        contacts = 0;
        bobPhase = 0;
        restY = 0;
        settleTicks = 0;
    }
};

} // namespace od::port::dropped_item_state
