// The pirates' tank of the Saul Kada pirate attack (ST29 / ST3B; pool 0x20 type 0xd, exe 0x46a290), see
// content/saul_kada/pirate_tank.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x46a290: drives around the player, its turret tracking him, lobbed cannon shells and machine-gun bursts;
// rideable; destroyed: two explosions, its drops, a Servbot jumps out
void pirateTankUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
