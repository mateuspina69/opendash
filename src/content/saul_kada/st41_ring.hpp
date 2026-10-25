// The solid ring of the pirate base's gate rooms (ST41 / ST42 static type 0x31, exe 0x5acaa0), see
// content/saul_kada/st41_ring.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5acaa0: a solid ring of 4 pieces (variant 0 spawns pieces 1..3, each turned 0x400 and pushed out 0x1c00;
// additive)
void solidRingUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
