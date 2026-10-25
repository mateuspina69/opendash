// The class states of st28_fire_bomb.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::st28_fire_bomb_state {

// The bomb's bytes: its travel yaw (its header's parameters 2 / 3), its damage (the rotation's spare lane); its class
// block: its hit box, its boss, a centre's two orbiting bombs, its homing time, its turn rate, its status, its fall's
// result, the hum's timer
struct BombFields : ActorClassState {
    explicit BombFields(ProjectileActor& actor) : travelYaw(actor.params, 2), damage(actor.rotation.spare), hitBox(actor.hitBox) {}
    BytesField<int16_t, 4> travelYaw;
    int16_t& damage;
    HitBoxHead& hitBox;
    ActorRef<AnimatedActor> boss;
    ActorRef<ProjectileActor> firstOrbiter;
    ActorRef<ProjectileActor> secondOrbiter;
    int16_t homingTime = 0;
    uint16_t turnRate = 0;
    uint8_t status = 0;
    uint8_t spare = 0;
    uint8_t fallResult = 0;
    uint8_t humTimer = 0;
};

} // namespace od::port::st28_fire_bomb_state
