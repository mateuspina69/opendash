// The carried NPC, class 0x4f2ee0 (pool 0x20 type 0x71): content/calbania/carried_npc.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4f2ee0: the NPC's update (ActorUpdateFn)
void carriedNpcUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
