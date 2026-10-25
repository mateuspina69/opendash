// The enemy shot (pool 0x40 type 9 of the stage tables that have it, exe 0x5c52f0: the ST3A sky ship's and the ST28
// guns' shots): it flies along its rotation for its ticks, a hit entry hurting the player (the shield stops it); a wall
// queues its breakable-wall hit and pops it, a body sparks it, its end pops it.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

constexpr uint8_t kEnemyShotType = 9;

// The shot's fields: its ticks and attack (the spawner's), its hit box (the projectile's tail)
struct EnemyShotFields {
    explicit EnemyShotFields(const ProjectileActor& actor) : EnemyShotFields(const_cast<ProjectileActor&>(actor)) {}
    explicit EnemyShotFields(ProjectileActor& actor) : ticks(actor.params, 0), attack(actor.params, 2), hitBox(actor.hitBox) {}
    BytesField<int16_t, 4> ticks;
    BytesField<uint16_t, 4> attack;
    HitBoxHead& hitBox;
};
// the shot's states (+8)
enum class EnemyShotState : uint8_t { Launch = 0, Fly = 1 }; // exe 0x5c5390 / 0x5c5440
// the variant's bits: & kShotSizeMask the size (radius, spark, pop), >> 2 & 3 the speed, kShotNoMover: no actor
// collision
constexpr uint8_t kShotSizeMask = 0x03;
constexpr uint8_t kShotNoMover = 0x80;

// exe 0x5c52f0 (pool 0x40 type 9)
void enemyShotUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
