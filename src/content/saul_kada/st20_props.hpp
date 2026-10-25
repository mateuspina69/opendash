// The ST20 props (static type 1, exe 0x5904d0), content/saul_kada/st20_props.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5904d0 (static ST20 type 1): props (a solid gong that rings when hit, a spinner, a swinger)
void st20PropUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
