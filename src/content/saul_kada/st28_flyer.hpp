// The fire ruin's flyer (ST28 pool 0x20 type 0x10 sub 0, exe 0x46c640), see content/saul_kada/st28_flyer.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x46c640: appears behind the player, sways and bobs above the ground, swoops away; stunned it drops pickups or a
// reaverbot (pool 0x20 type 5 variant 4); explodes when destroyed
void flyerUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
