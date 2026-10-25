// The class states of reaverbot_turret.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::reaverbot_turret_state {

// The turret's own fields: its spawn parameters and its class block
struct TurretFields : ActorClassState {
    explicit TurretFields(AnimatedActor& actor) : rangeIndex(actor.params[0]), bulletVariant(actor.params[3]) {}
    FieldAlias<int8_t, uint8_t> rangeIndex; // its walk-out range [0x88eef8]
    uint8_t& bulletVariant;
    HitBoxHead hitBox;
    uint16_t flags = 0;        // kTurret*
    uint16_t timer = 0;
    uint32_t pushOut = 0;      // the push-out's result of this tick
    uint16_t neighbour = 0;    // game/enemy_neighbours
    int16_t target = 0;        // the walk's target yaw
    int16_t turnTicks = 0;     // ticks before the walk takes a new target
    uint16_t yawReference = 0; // the boxes' frame
    uint8_t chargeSlot = 0;
    uint8_t bulletSlot = 0;
    uint8_t bulletHold = 0;    // ticks the bullet is still held
    uint8_t firing = 0;
    int16_t travelled = 0;     // the distance walked out (behaviours 1 / 3)

    // the flags' low byte / its bits set (the exe's byte read-modify-write) and the high byte's
    uint8_t lowFlags() const { return uint8_t(flags); }
    void setLowFlags(uint8_t bits) { flags = uint16_t(flags | bits); }
    void setHighFlags(uint8_t bits) { flags = uint16_t(flags | uint16_t(bits) << 8); }

    void clearBlock() override {
        hitBox = {};
        flags = 0;
        timer = 0;
        pushOut = 0;
        neighbour = 0;
        target = 0;
        turnTicks = 0;
        yawReference = 0;
        chargeSlot = 0;
        bulletSlot = 0;
        bulletHold = 0;
        firing = 0;
        travelled = 0;
    }
};

} // namespace od::port::reaverbot_turret_state
