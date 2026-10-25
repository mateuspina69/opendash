// The spawners that place Roll by the room's entrance in the chapter after the crash landing (pool 0xe0 type 0xa of
// the ST08 / ST09 / ST0D tables; content/yosyonke/roll_spawners.cpp).
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x41af40 (ST08): by the flags 0x5e1 / 0x5c1 Roll following (exe 0x445b00) or waiting at Joe's lab (exe 0x443330)
void rollSpawnerST08Update(const PlayerEnv& env, uint32_t actor);
// exe 0x41b1d0 (ST09, before the junk shop): Roll by the entrance the player came out of (exe 0x41b270)
void rollSpawnerST09Update(const PlayerEnv& env, uint32_t actor);
// exe 0x41b440 (ST0D): the tracks' cell, Roll by the entrance (exe 0x4e4c20), the region flags
void rollSpawnerST0DUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
