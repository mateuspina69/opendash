// The patrolling blade trap of the Nino ruin (static type 0x45, ST36..ST38), content/nino/blade_trap.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5b2ba0: states [0x8d07e4] (init, run, dying), the run's sub-states [0x8d07f0] (start, patrol, attack,
// retract)
void bladeTrapUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
