// The debris of a broken wall segment (static type 0xc of the global table; spawned by the breakable walls' break,
// game/breakable_walls, and the wall crumble, game/wall_crumble): 20 pieces of the segment's texture thrown out around
// the break and falling to the floor (ordering-table quads), blinking after 0x3e0 ticks, gone after 0x400.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/room_grid.hpp"

namespace od::port {

// the debris of the pool entry `pe`'s segment at the slot `slot` (a fresh static slot), at (x, y): its variant the
// entry's reaction (& 0xf), updated in the late loop
void placeWallDebris(uint32_t slot, const SegmentPoolEntry& pe, int16_t x, int16_t y, int16_t z);

// exe 0x565af0 (static global type 0xc)
void wallDebrisUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
