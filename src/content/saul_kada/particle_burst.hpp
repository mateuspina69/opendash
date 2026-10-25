// The burst of particles (static type 0x4c sub 0: ST27 / 28 / 34 / 42 / 55, exe 0x5b5a00), the ruin flyers' shot,
// content/saul_kada/particle_burst.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5b5a00 (static type 0x4c sub 0): a burst of 10 particles (0xf0 discs + a hit entry)
void particleBurstUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
