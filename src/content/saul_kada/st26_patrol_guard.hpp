// The ST26 lane guard (pool 0x20 type 0x3b sub 1, exe 0x4b4b00), see content/saul_kada/st26_patrol_guard.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4b4b00: patrols back and forth along its facing axis, charges the player inside its lane, knocked back by
// heavy hits, gives way to its neighbours, dies in an explosion with drops
void patrolGuardUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
