// The ice floor's marker of the sub-ruins ST2D / ST2E / ST2F (pool 0xe0 type 1, exe 0x425b10), see
// content/saul_kada/ice_floor_marker.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x425b10: the player's floor slippery -- everywhere in ST2E, in ST2D / ST2F but their first room / room 7, else
// on the stage's ice blocks (static type 0x1f)
void icePlayerMarkerUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
