// The fire ruin's guard (ST28 pool 0x20 type 0x6c sub 0, exe 0x4f1870), see content/saul_kada/st28_guard.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4f1870: watches, shoots, charges while the player is within 0x708 of its home
void st28GuardUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
