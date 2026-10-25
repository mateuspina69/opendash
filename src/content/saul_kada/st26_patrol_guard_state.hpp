// The class states of st26_patrol_guard.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::st26_patrol_guard_state {

// The guard's bytes: a count (its first parameter byte), its range (parameter bytes 2 / 3); its hit box (its contact
// motion: the hit's yaw), its home (x / z), its patrol heading, a wall's yaw, the knock-back's yaw, its neighbour word,
// its flags, a wall hit, dead, its leg
struct GuardFields : ActorClassState {
    explicit GuardFields(AnimatedActor& actor) : count(actor.params[0]), countSigned(actor.params[0]), range(actor.params, 2), flags(actor.townBirdHits), wallHit(actor.townBirdHitsHigh), dead(actor.hopperTimer) {}
    uint8_t& count;
    FieldAlias<int8_t, uint8_t> countSigned;
    BytesField<uint16_t, 4> range;
    uint8_t& flags;
    uint8_t& wallHit;
    uint8_t& dead;
    HitBox hitBox;
    uint16_t homeX = 0;
    uint16_t homeZ = 0;
    uint16_t heading = 0;
    uint16_t wallYaw = 0;
    uint16_t knockYaw = 0;
    uint16_t neighbour = 0;
    uint8_t leg = 0;

    void clearBlock() override {
        hitBox = {};
        homeX = 0;
        homeZ = 0;
        heading = 0;
        wallYaw = 0;
        knockYaw = 0;
        neighbour = 0;
        leg = 0;
    }
};

} // namespace od::port::st26_patrol_guard_state
