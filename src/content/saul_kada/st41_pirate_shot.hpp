// The pirates' shots of the gate rooms (ST41 / ST42 pool 0x40 type 0x31, exe 0x5d0f40), see
// content/saul_kada/st41_pirate_shot.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

constexpr uint8_t kPirateShotType = 0x31;
// its variants: a falling bomb, a homing shot rising / thrown up to the player's height
enum class PirateShotVariant : uint8_t { Bomb = 0, Rising = 1, Thrown = 2 };

// exe 0x5d0f40: the falling bomb (its ground hit: an impact and sound 0x2e6), or a homing shot that levels at the
// player's height, then bobs (its life 0xf0); 0x2c billboards
void pirateShotUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
