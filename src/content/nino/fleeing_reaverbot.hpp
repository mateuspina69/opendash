// The small reaverbot of the Nino ruin that wanders, flees the player and the shots and pops up when hit (pool 0x20
// type 0x42, ST36 / ST38), content/nino/fleeing_reaverbot.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4c1180: states [0x894b94] (init, run, dying), the run's sub-states [0x894ba0] (wander, pop up)
void fleeingReaverbotUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
