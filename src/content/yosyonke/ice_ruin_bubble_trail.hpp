// The bubble trail of the ice ruin's torpedo (ST2F static stage type 0x68, exe 0x5c2560; the torpedo 0x5cac60 of
// content/nino/ruin_swimmers spawns it), content/yosyonke/ice_ruin_bubble_trail.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5c2560: the bubble trail's update (0x2c billboards)
void bubbleTrailUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
