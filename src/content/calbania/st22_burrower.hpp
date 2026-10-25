// The ST22 burrower, class exe 0x457480 (pool 0x20 type 8 sub 0), content/calbania/st22_burrower.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x457480 (ST22 pool 0x20 type 8 sub 0; the encounter / formation records 0x8dbad0..): the burrower -- rises
// out of the ground, walks at the player (variant 0) or around the neighbours (variant 1), throws (pool 0x40 type
// 4) / lunges (pool 0xa0 type 4), flinches at half hp, sinks away far from the player, explodes (drops) when dead
void burrowerUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
