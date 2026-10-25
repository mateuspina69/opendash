// The ST22 fire projectiles, exe 0x5c2a60 (pool 0x40 stage type 1), content/calbania/st22_fire_shots.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// ---- projectiles (pool 0x40 stage tables) ------------------------------------------------------------------------------
// exe 0x5c2a60 (ST22 stage type 1): fire projectiles by variant (bouncing ball, homing lob, ember, rising bomb, shard)
void st22FireShotUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
