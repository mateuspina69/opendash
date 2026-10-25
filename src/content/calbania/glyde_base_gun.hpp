// The gun emplacements of Glyde's base, class exe 0x4c0350 (pool 0x20 type 0x41 of ST30 sub 1; type 0x65 elsewhere),
// content/calbania/glyde_base_gun.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4c0350: a gun emplacement -- fixed, its yaw (clamped to its arc when it has one) and its barrel's pitch track
// the nearest of the player and Appo / Dah; it lobs a shell (pool 0x40 type 7) at middle range and fires shots (pool
// 0x40 type 9) when near; destroyed, it explodes and leaves the drops
void glydeBaseGunUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
