// The ST27 enemy respawner (static type 0x27, exe 0x5a9d80), content/saul_kada/st27_respawner.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5a9d80 (static ST27 type 0x27): the room's enemy respawner (spawns near / despawns far)
void respawnerUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
