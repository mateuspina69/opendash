// The ST13 wall-break burst controller 0x41a500 (pool e0 type 9), content/manda/st13_wall_break.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x41a500 (pool e0 type 9: ST13; spawned by a breakable-wall reaction with the wall's records, game/wall_crumble
// WallCrumbleFields): the wall-break burst. Tick 0: the global static type 0xc debris and type 8 explosion, the wall's
// cell to variant 2 (exe 0x58d5f0); then 16 ticks of billboard rings (step 0xc0) rising and fading, the texture cell by
// tick; freed at tick 0x10.
void wallBreakBurstUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
