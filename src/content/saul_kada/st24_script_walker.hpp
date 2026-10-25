// The ST24 script walker (pool 0x20 type 0x3a, exe 0x4b2650), content/saul_kada/st24_script_walker.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4b2650 (pool 0x20 type 0x3a: ST24): a cast member walking through its script's steps
void scriptWalkerUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
