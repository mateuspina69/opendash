// The class states of st28_guard.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::st28_guard_state {

// The guard's own bytes (0x20 cleared at the bind): its hit box, a timer, the shot's animation index, its flags, its
// head's yaw and that yaw's target, its home (x / z), the shot's sound and type
struct GuardFields : ActorClassState {
    explicit GuardFields(AnimatedActor& actor) : shotType(actor.hopperBytes[2]) {}
    uint8_t& shotType;
    HitBoxHead hitBox;
    int16_t timer = 0;
    int16_t shotAnim = 0;
    uint8_t flags = 0;
    uint16_t headYaw = 0;
    uint16_t headTarget = 0;
    int16_t homeX = 0;
    int16_t homeZ = 0;
    uint16_t shotSound = 0;

    void clearBlock() override {
        hitBox = {};
        timer = 0;
        shotAnim = 0;
        flags = 0;
        headYaw = 0;
        headTarget = 0;
        homeX = 0;
        homeZ = 0;
        shotSound = 0;
    }
};

} // namespace od::port::st28_guard_state
