// The ice ruin's ST2D / ST2E barrier with its guard (static type 0x2a, exe 0x5aa770), content/yosyonke/ice_ruin_flag_barrier.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5aa770 (static ST2D / ST2E type 0x2a): a semi-transparent barrier with its guard, gone by its flag (sound 0x255)
void flagBarrierUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
