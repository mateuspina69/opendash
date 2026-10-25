// The item on its pedestal in the ruin under Nino (ST35 sub 5, ST37 sub 6: static type 0x40),
// content/nino/pedestal_item.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5b1450 (states [0x8d06a4]): variant 0 turns when its parameter asks; used (talked to) -> the event 0x20022
// (window 1, message 0x13) and its flag; then the flags 0x90 off, 0x91 / 0x92 on, freed
void pedestalItemUpdate(const PlayerEnv& env, uint32_t actor);
// ST37's key on its pedestal (static type 0x40 variant 1, turning, its flag 0x3e9) at the 16.16 point (x, -0x10, z):
// spawned by the room's handler and by the last gauge reaverbot down; false when no static slot was free
bool spawnPedestalKey(int32_t x, int32_t z);

} // namespace od::port
