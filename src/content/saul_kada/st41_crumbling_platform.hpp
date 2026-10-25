// The crumbling platform of the pirate base's gate rooms (ST41 / ST42 static type 0x1f sub 1, exe 0x5a4c60),
// see content/saul_kada/st41_crumbling_platform.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5a4c60: after the gate (flag 0x155) it cracks under the player (its model swapped at 0x28, sound 0x2e7) and
// breaks (sound 0x2e8) into 3 falling pieces
void crumblingPlatformUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
