// The class states of throwable.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::throwable_state {

// The object's fields (its class block): its hit box, the shadow switch, the room collision's result, the yaw it travels
// along, its life's ticks, the life armed, its kind's index (box, carry offset)
struct ThrowableFields : ActorClassState {
    explicit ThrowableFields(StaticActor& actor) : hitBox(actor.hitBox), lifeArmed(actor.gunshipCooldown()), kind(actor.gunshipAttackCooldown()) {}
    HitBoxHead& hitBox;
    uint8_t& lifeArmed;
    uint8_t& kind;
    uint8_t shadow = 0; // kThrowableNoShadow
    uint8_t contact = 0; // kThrowable* of the last room collision
    int16_t travelYaw = 0;
    int16_t lifeTicks = 0;

    void clearBlock() override {
        shadow = 0;
        contact = 0;
        travelYaw = 0;
        lifeTicks = 0;
    }
};

} // namespace od::port::throwable_state
