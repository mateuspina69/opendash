// The sand pit (pool e0 type 0x23: ST48 sub 1, ST5B, exe 0x427290), content/saul_kada/quicksand.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x427290 (pool e0 type 0x23): the sand pit -- inside its radius the player is pulled to its point (and slowed),
// and sand sprays from his feet while he moves
void quicksandUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
