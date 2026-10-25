// The class states of sand_trap.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::sand_trap_state {

// The trap's own bytes: its hit box, a timer, the presses left to break free
struct TrapFields : ActorClassState {
    explicit TrapFields(AnimatedActor&) {}
    HitBoxHead hitBox;
    uint16_t timer = 0;
    uint16_t escapePresses = 0;

    void clearBlock() override {
        hitBox = {};
        timer = 0;
        escapePresses = 0;
    }
};

} // namespace od::port::sand_trap_state
