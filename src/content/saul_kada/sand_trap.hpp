// The sand trap (pool 0x20 type 0x61: ST27 / 34 / 40..42, exe 0x4e75d0), content/saul_kada/sand_trap.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4e75d0 (pool 0x20 type 0x61): the sand trap that grabs and drains the player
void sandTrapUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
