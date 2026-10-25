// The class states of st13_spin_blade.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::st13_spin_blade_state {

// The blade's own bytes: its hit box, the blade's turn (the part's decoded yaw), its top speed, the sound's cooldown,
// the ticks to its restart; its spin acceleration is the spin's spare lane
struct BladeFields : ActorClassState {
    explicit BladeFields(AnimatedActor& actor) : spinAccel(actor.spin.spare) {}
    int16_t& spinAccel;
    HitBoxHead hitBox;
    uint16_t bladeTurn = 0;
    int16_t topSpeed = 0;
    uint16_t soundTicks = 0;
    uint8_t restartTicks = 0;

    void clearBlock() override {
        hitBox = {};
        bladeTurn = 0;
        topSpeed = 0;
        soundTicks = 0;
        restartTicks = 0;
    }
};

} // namespace od::port::st13_spin_blade_state
