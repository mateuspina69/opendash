// The models put down by the player (static types 4 and 0xa of the global table): a model set 0xa0 from the player
// along the quadrant of its yaw, stepped toward a target when its sub-state is set from outside, and a model set beside
// the player that turns when its sub-state is set from outside. Their spawners are not traced (no ported class
// spawns them).
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x563180 (static global type 4; states 0x5631a0 / 0x563280 / 0x5afdd0): the placed model
void placedModelUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x564d60 (static global type 0xa; states 0x564d80 / 0x564e50 / 0x5afdd0): the turning model
void turningModelUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
