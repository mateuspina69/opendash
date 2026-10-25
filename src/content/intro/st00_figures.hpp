// The display figures of ST00 (pool 0x20, stage class type 2, exe 0x449cc0): models on display that turn their head to
// the player, loop, play random animations, take hits, scroll their texture or blink (content/intro/st00_figures.cpp).
#pragma once

#include <cstdint>

namespace od::port {

struct PlayerEnv;

// exe 0x449cc0: a display figure's update (registered in content/intro/register.cpp)
void st00FigureUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
