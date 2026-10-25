// The class states of gun_turret.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::gun_turret_state {

// The turret's own bytes: its hit box, the gun's pitch / yaw / roll, the damage taken lately, the recoil's second
// pass, the smoke's ticks, the ticks the player is far, the sweep's timer, the aim's timer, the shots fired
struct TurretFields : ActorClassState {
    explicit TurretFields(AnimatedActor& actor) : smokeTicks(actor.guardianBits), aimTimer(actor.hopperBytes[0]), shots(actor.hopperBytes[1]) {}
    uint8_t& smokeTicks;
    int8_t farTicks = 0;
    uint8_t& aimTimer;
    uint8_t& shots;
    HitBoxHead hitBox;
    int16_t gunPitch = 0;
    int16_t gunYaw = 0;
    int16_t gunRoll = 0;
    int16_t recentDamage = 0;
    uint8_t recoiled = 0;
    uint8_t sweepTimer = 0;

    void clearBlock() override {
        farTicks = 0;
        hitBox = {};
        gunPitch = 0;
        gunYaw = 0;
        gunRoll = 0;
        recentDamage = 0;
        recoiled = 0;
        sweepTimer = 0;
    }
};

} // namespace od::port::gun_turret_state
