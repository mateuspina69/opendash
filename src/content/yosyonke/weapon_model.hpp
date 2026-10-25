// The special weapon's turning model, class 0x4ec4f0 (pool 0x20 type 0x65 of ST04 / ST19; content/yosyonke/weapon_model.cpp).
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4ec4f0 (ST04 / ST19 type 0x65; spawned by the special-weapon screens 0x545b80 / 0x548380 / 0x5edac0, variant =
// weapon - 1): the special weapon's model, turning after 0x1f ticks
void weaponModelUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
