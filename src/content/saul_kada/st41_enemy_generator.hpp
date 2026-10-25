// The enemy generator of the pirate base's gate rooms (ST41 / ST42 pool 0x20 type 0x10, exe 0x46d210), see
// content/saul_kada/st41_enemy_generator.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x46d210: releases field reaverbots (pool 0x20 type 5 variant 4) while fewer than 3 are alive and the player is
// within 0x5dc; its hit points, a flinch, an explosion, its drops and the defeat count when destroyed
void enemyGeneratorUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
