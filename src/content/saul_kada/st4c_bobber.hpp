// The ST4C bobbing object (static type 0x40, exe 0x5b1760), content/saul_kada/st4c_bobber.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5b1760 (static ST4C type 0x40): a bobbing, spinning object with a hit capsule
void bobberUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
