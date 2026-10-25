// Glyde's tanks at the main gate of his base, class exe 0x4bedc0 (pool 0x20 type 0x40 sub 1 of ST30 sub 2),
// content/calbania/glyde_base_tank.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4bedc0: one of Glyde's tanks -- a tracked vehicle the player can stand on; it patrols, chases and circles the
// player, its turret tracks the player's predicted place and fires bursts of the stage shot (pool 0x40 type 9).
// Destroyed, a Birdbot bails out; the last of the gate's wave sets flag 0x22 and drops the key (static type 0x42)
void glydeBaseTankUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
