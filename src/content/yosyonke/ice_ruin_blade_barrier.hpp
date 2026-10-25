// The ice ruin's spinning blade barrier (ST2D static type 0x30, exe 0x5ac270), content/yosyonke/ice_ruin_blade_barrier.cpp.
// By its variant:
//   0  the hub: spins its roll by 0x18 a tick, a hit capsule along both blade axes; spawns the blocker and the four
//      segments (each a quarter turn on)
//   1  a blade segment (0x28 hit points, the damaged model below 0x14); broken: sound 0x251, six debris pieces,
//      0xb4 ticks hidden, then faded back in at full hit points; marks its bit in the hub's mask while standing
//   2  the blocker: an invisible solid that follows the player while the hub's quadrant facing them holds a segment
//   3  a debris piece (a random model), falls until the move test fails, then freed
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5ac270 (static ST2D type 0x30): the blade barrier's update
void bladeBarrierUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
