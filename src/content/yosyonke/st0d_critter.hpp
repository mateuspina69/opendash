// The shy critter of ST0D, class 0x509830 (pool 0x20 type 0x7e; content/yosyonke/st0d_critter.cpp).
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x509830: wanders, runs from the player, knocked away by hits (the lifted state 2 throws: the carry routines are
// not ported)
void shyCritterUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
