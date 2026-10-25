// The ST27 scrolling glow (pool 0xa0, exe 0x4391b0), content/saul_kada/st27_scroll_glow.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4391b0 (pool 0xa0: ST27 sub 2): a glowing billboard with a scrolling texture (draw command 0x2c)
void scrollGlowUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
