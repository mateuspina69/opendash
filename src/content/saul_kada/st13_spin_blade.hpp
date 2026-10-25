// The ST13 spinning blade (pool 0x20 type 0x20, exe 0x481980), content/saul_kada/st13_spin_blade.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x481980 (pool 0x20 type 0x20: ST13): a spinning blade / fan (slows to a stop when hit; hit capsules on its bones)
void spinBladeUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
