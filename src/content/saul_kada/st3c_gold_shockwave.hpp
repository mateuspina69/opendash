// The gold-statue boss's shockwaves (ST3C static type 0x5d, exe 0x5bcc10), see
// content/saul_kada/st3c_gold_shockwave.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

constexpr uint8_t kGoldShockwaveType = 0x5d;

// exe 0x5bcc10: sub 0 the shockwaves' spawner (variants 0..2); subs 1 / 2 the growing, spinning shockwave models it
// spawns
void goldShockwaveUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
