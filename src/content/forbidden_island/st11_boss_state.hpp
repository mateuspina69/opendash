// The class states of st11_boss.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::st11_boss_state {

// The fighter's class block: its hit box (the capsule's result; the contact angle of the hit), the player's planar
// distance, the knockback's angle, two countdowns, its flags, the wall touched, the hop's side / phase / apex, dead
struct FighterFields : ActorClassState {
    explicit FighterFields(AnimatedActor& actor) : flags(actor.hopperBytes[4]), wall(actor.hopperBytes[5]), hopSide(actor.townBirdHits), hopPhase(actor.townBirdHitsHigh), hopApex(actor.hopperTimer), strafeYaw(actor.rotation.spare), tintLow(actor.tint, 0), tintHigh(actor.tint, 1) {}
    uint8_t& flags; // kFighter*
    uint8_t& wall; // 1: the push-out met a wall this tick
    uint8_t& hopSide; // 1: circling to the right (the yaw turned forward)
    uint8_t& hopPhase; // HopPhase
    uint8_t& hopApex; // the hop's falling animation started
    int16_t& strafeYaw;
    PartField<uint8_t, uint16_t> tintLow;
    PartField<uint8_t, uint16_t> tintHigh;
    HitBox hit;
    int32_t distance = 0;
    uint16_t knockbackAngle = 0;
    int16_t timer = 0;
    int16_t timeout = 0;
    uint8_t dead = 0; // the hit capsule harmless

    void clearBlock() override {
        hit = {};
        distance = 0;
        knockbackAngle = 0;
        timer = 0;
        timeout = 0;
        dead = 0;
    }
};

} // namespace od::port::st11_boss_state
