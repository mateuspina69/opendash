// The spray class 0x5c7420 of ST15 / ST16 (pool 0x40 type 0x13), content/nino/st15_spray.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5c7420 (pool 0x40 type 0x13: ST15 / ST16): the spray of the group members (content/nino/st15_cls: three
// puffs, hit-list entries, billboards)
void st15SprayUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
