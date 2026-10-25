// The pirate base's breakable crate (static ST3B type 1, exe 0x590f30), content/saul_kada/st3b_crate.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x590f30 (static ST3B type 1): a breakable crate (hit points, the flash, a blast when destroyed; solid)
void breakableUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
