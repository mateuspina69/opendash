// The Birdbot tank outside Glyde's base, class exe 0x4bd080 (pool 0x20 type 0x40 sub 0 of ST1F),
// content/calbania/st1f_birdbot_tank.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4bd080: the ST1F Birdbot tank -- its turret tracks the nearest of the player / Appo and Dah, keeps its
// distance circling the arena centre, fires bursts of the stage shot (pool 0x40 type 9) from four muzzles, tilts with the ground slope, stops while the player stands on it; dies in two explosions leaving a
// knocked-down Birdbot (pool 0x20 type 0x2d sub 0) and the drops
void st1fBirdbotTankUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
