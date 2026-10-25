// The sliding doors of the pirate base's gate rooms (ST41 / ST42 static type 0x58, exe 0x5ba660), see
// content/saul_kada/st41_sliding_door.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5ba660: a sliding door pair over a grid cell (variant 0 spawns its twin, variant 1): opens (its cell's segment
// variant 1, sliding 8 ticks, sounds 0xba / 0xbd) when the player is in front of it and its lock flag is clear (the
// flag 0x710 + its number mirrors the lock)
void slidingDoorUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
