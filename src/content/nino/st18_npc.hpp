// The ST18 standing NPC, class 0x443aa0 (pool 0x20 type 0, variant 6, at story 9 / 0xa): content/nino/st18_npc.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x443aa0: the NPC's update (ActorUpdateFn)
void st18NpcUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
