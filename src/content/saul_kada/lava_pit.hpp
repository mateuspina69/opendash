// ST28 sub 3, the fire ruin's lava pit: its controller (pool e0 type 0x1e, exe 0x426a40), the creature's mover of
// script 0x47's end (pool e0 type 0x1f, exe 0x426b20), the flames (pool 0x60 type 0x44, exe 0x5b29b0) and the creature
// that surfaces at random spots (pool 0x60 type 0x40 sub 3, exe 0x5b18c0). See content/saul_kada/lava_pit.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x426a40 (pool e0 type 0x1e): the flames / the creature's spots
void pitControllerUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x426b20 (pool e0 type 0x1f: ST28): moves the creature between random spots away from the player
void pitCreatureMoverUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5b29b0 (pool 0x60 type 0x44): a flame (draw command 0x2e)
void pitFlameUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5b18c0 (pool 0x60 type 0x40 sub 3): the surfacing creature (event 0x28 + flag 0x56 when hit)
void pitCreatureUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
