// The ST4B cast of script 0x59: the talking cast member (pool 0x20 type 0x1a, exe 0x47dd10) and the item Megaman holds
// (static type 0x40, exe 0x5b23d0), content/saul_kada/st4b_cast.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x47dd10 (pool 0x20 type 0x1a: ST4B): a talking cast member of script 0x59 (mouth flaps)
void talkerUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5b23d0 (static ST4B type 0x40): the item Megaman holds (variant 0 follows his hand)
void heldItemUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
