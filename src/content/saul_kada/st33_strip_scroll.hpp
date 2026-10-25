// The ST33 VRAM strip scroller (pool e0 type 0x1a, exe 0x426020), content/saul_kada/st33_strip_scroll.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x426020 (pool e0 type 0x1a: ST33): the 16 x 64 strip scroller of 0x425eb0 / 0x426180 for (0x630, 0x180) into
// (0x630, 0x140), no stop flag
void st33StripScrollUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
