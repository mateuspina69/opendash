// ST12 / ST13 / ST14 actor classes, content/manda/st12_14.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x59c9b0 (ST14 static type 0x18): the rolling boulder (back and forth between two points, a camera shake on
// the walls)
void rollingBoulderUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x59a7a0 (ST12 static type 0x13): the tumbling block (flag-started slides and rocking, lightning bolts)
void tumblingBlockUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x47e430 (pool 0x20 type 0x1b: ST12): the walker that drops in (walks until blocked, falls bouncing off walls)
void dropInWalkerUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4892c0 (pool 0x20 type 0x26: ST13): the gun turret (aims at the player, fires bursts, staggers, sparks and
// dies)
void st13GunTurretUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
