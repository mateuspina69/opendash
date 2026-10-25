// The burrowing sand enemy of Saul Kada (ST27 / ST34; pool 0x20 type 0x35, exe 0x4a9eb0), see
// content/saul_kada/sand_burrower.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4a9eb0: waits buried until the player is within 0xbb8 of its home, surfaces, paces between its two home points
// (its spawn point and the point 0x300 ahead), then leaps at the player or charges / spits shots (the stage's pool-0x40
// type 0); burrows back home when the player leaves; its enemy group's marks while attacking
void sandBurrowerUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
