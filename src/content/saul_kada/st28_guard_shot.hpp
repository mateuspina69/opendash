// The fire-ruin guard's lobbed second shot (ST28 pool 0x40 type 0x1b, exe 0x5ca4f0), see
// content/saul_kada/st28_guard_shot.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

constexpr uint8_t kGuardShotType = 0x1b;

// exe 0x5ca4f0: lobbed at the player (its speed by his distance); bursts on a hit or a wall
void guardShotUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
