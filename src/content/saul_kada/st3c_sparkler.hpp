// The sparkles at the player in the gold statue's room (ST3C pool 0xe0 type 0x28, exe 0x427a70, record 0x8ab940), see
// content/saul_kada/st3c_sparkler.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x427a70: the player below it (and standing): his wading set, then every other tick a sparkle at his
// extrapolated position
void sparklerUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
