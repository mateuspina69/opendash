// A figure of the fire ruin's room scripts that fades in (ST27 pool 0x20 type 0x53 sub 1, exe 0x4df230), see
// content/saul_kada/ruin_fading_figure.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4df230 (room-script records 0x8a5618): waits 0x1e ticks, fades in (its colour, a semi-transparent draw)
// playing animation 5, then idles in animation 0
void fadingFigureUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
