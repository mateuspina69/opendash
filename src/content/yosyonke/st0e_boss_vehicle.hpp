// ST0E: the vehicle chase. The player defends a vehicle (ST0E static type 0x4e, content/yosyonke/st0e_defended_vehicle:
// the room records' actor 1, its HP carried over in the stage block when flag 0x5a1 is set) from a boss vehicle (pool
// 0x20 type 0x7a, here) that drives along its yaw carrying a turret (pool 0x20 type 0x7b,
// content/yosyonke/st0e_turret); the vehicle drops carrier crates (content/yosyonke/st0e_carrier_crate) and carries Servbot
// soldiers (content/yosyonke/st0e_servbots). Hypothesis (from the code only): the pirates' attack on the train in the field.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x501d70: pool 0x20 type 0x7a sub 0 (ST0E subs 2 / 5 records) -- the boss vehicle; spawns the turret (0x7b)
void bossVehicleUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5bc3f0: pool 0x60 stage type 0x5c -- the fireball the vehicle charges on its bone 1 and launches
void vehicleFireballUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
