// The pirates' gun turret (pool 0x20 type 0xf: ST29 / ST2B, exe 0x46bad0), content/saul_kada/gun_turret.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x46bad0 (pool 0x20 type 0xf): the pirates' gun turret (a Servbot jumps out when it is destroyed)
void pirateGunTurretUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
