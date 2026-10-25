// The flame ribbon between the lava boss's two orbiting fire bombs (ST28 pool 0xa0 type 0x15, exe 0x4383d0), see
// content/saul_kada/st28_bomb_ribbon.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"

namespace od::port {

// exe 0x438720(centre): the ribbon between the centre's two orbiting bombs
void spawnBombRibbon(uint32_t firstBomb, uint32_t secondBomb);
// exe 0x4383d0: two textured quads between the bombs while the first lives
void bombRibbonUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
