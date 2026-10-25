// ST10 pool-0x20 class 0x48c160, content/forbidden_island/st10_cls.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// 0x48c160 (pool 0x20, ST10 sub 0): a two-form enemy (wobbling roller -> morphs when hit into a chaser that dies)
void st10RollerUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
