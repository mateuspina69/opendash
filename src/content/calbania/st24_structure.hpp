// The ST24 story structure, class 0x49d110 (pool 0x20, ST24 type 0x30): content/calbania/st24_structure.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x49d110: the structure's update (ActorUpdateFn)
void st24StructureUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
