// A rising ring of light of the ruins (pool 0xa0 type 0x14 of ST26 / 27 / 34 / 40 / 41 / 42, exe 0x4382f0), see
// content/saul_kada/ruin_light_ring.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4382f0: a ring record rising 0x40 a tick for 8 ticks, its colour fading
void lightRingUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
