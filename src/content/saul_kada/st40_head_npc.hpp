// The ST40 standing NPC that turns its head (pool 0x20 type 0 sub 1, exe 0x445840),
// content/saul_kada/st40_head_npc.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x445840 (pool 0x20 type 0 sub 1: ST40): a standing NPC whose head turns toward the player / a point
void headNpcUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
