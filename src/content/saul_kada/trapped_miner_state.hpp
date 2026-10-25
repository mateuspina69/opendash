// The class states of trapped_miner.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::trapped_miner_state {

// The miner's own bytes: the idle's pause, the ticks to the next cry, the call's phase, the call's ticks left
struct MinerFields : ActorClassState {
    explicit MinerFields(AnimatedActor&) {}
    uint16_t pauseTicks = 0;
    uint16_t cryTicks = 0;
    uint8_t callPhase = 0;
    uint16_t callTicks = 0;

    void clearBlock() override {
        pauseTicks = 0;
        cryTicks = 0;
        callPhase = 0;
        callTicks = 0;
    }
};

} // namespace od::port::trapped_miner_state
