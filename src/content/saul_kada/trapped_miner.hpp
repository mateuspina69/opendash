// The miner trapped in the mine (ST2B sub 1: pool 0x20 type 0 variant 0, exe 0x4452d0),
// content/saul_kada/trapped_miner.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4452d0 (pool 0x20 type 0: ST2B sub 1): the trapped miner calling for help
void minerUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
