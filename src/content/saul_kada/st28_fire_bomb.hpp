// The lava boss's fire bombs (ST28 pool 0x40 type 0x17, exe 0x5c86e0), see content/saul_kada/st28_fire_bomb.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"

namespace od::port {

// its variants: a homing bomb (liftable, throwable), the invisible centre of two orbiting bombs, an orbiting bomb
enum class FireBombVariant : uint8_t { Homing = 0, Centre = 1, Orbiting = 2 };

// exe 0x5c9160(boss, point, angles, variant): a fire bomb at the s16 point with the boss's angles (a centre also its
// two orbiting bombs), its sound
void spawnFireBomb(const AnimatedActor& boss, const int16_t point[3], FireBombVariant variant);
// exe 0x5c86e0 (by its variant [0x8d13dc])
void fireBombUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
