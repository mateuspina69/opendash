// The ST52 cast: a standing cast member (pool 0x20 type 0x5a, exe 0x4e2c40) and one timed by its script's tick (type
// 0x75, exe 0x4fb0e0), content/saul_kada/st52_cast.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4e2c40 (pool 0x20 type 0x5a: ST52): a standing cast member with face tracks
void standingCastUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4fb0e0 (pool 0x20 type 0x75: ST52): a cast member timed by the script's step tick
void timedCastUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
