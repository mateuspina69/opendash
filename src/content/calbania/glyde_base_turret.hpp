// The wall turrets of Glyde's base, class exe 0x4e0f50 (pool 0x20 type 0x57 of ST30 sub 2),
// content/calbania/glyde_base_turret.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4e0f50: a wall turret -- fixed, it aims at the player within reach (yaw / pitch offsets eased toward a jittered
// aim) and fires bursts of the stage shot (pool 0x40 type 9) from its barrel; destroyed, it flashes, hides its barrel
// and smokes
void glydeBaseTurretUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
