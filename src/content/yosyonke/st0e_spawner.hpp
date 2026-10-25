// The ST0E encounter switch (pool 0xe0 type 0xa, exe 0x41b830): by its variant, the field encounters of the stage
// (game/encounter) run or are suspended as the player crosses an x line, with the formation picks of that side.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x41b830: ST0E e0 type 0xa: the field encounters' formation picks and their suspension by the player's x
void encounterSwitchUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
