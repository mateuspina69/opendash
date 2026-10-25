// The pirates' hovering bomb critter (ST48 pool 0x20 type 0x13, exe 0x472870), see content/saul_kada/bomb_critter.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x472870: pops out of the ground, hovers after the player, lights its fuse and blows up; liftable / throwable
void bombCritterUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
