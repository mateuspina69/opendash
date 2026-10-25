// ST0E (the vehicle chase, see st0e_boss_vehicle.hpp): the vehicle the player defends
// (content/yosyonke/st0e_defended_vehicle.cpp).
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5b5e70: static (pool 0x60) ST0E type 0x4e (the room records' actor 1) -- the vehicle the player defends: it
// keeps its distance behind the boss vehicle, signals the turret through the enemy-group list, shows a target gauge,
// sets the flags 0x584 / 0x6f0 when destroyed
void defendedVehicleUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
