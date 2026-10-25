// The class states of glyde_base_rooms.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::glyde_base_rooms_state {

// a walker's target (the player or a box; content/nino/glyde_box_walkers)
struct WalkerTarget : ActorClassState {
    explicit WalkerTarget(AnimatedActor&) {}
    uint32_t target = 0;

    void clearBlock() override {
        target = 0;
    }
};

} // namespace od::port::glyde_base_rooms_state
