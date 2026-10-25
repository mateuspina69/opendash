// ST3B (the Saul Kada pirate base, story 12): the fixed bomb turret (pool 0x20 type 0x12 sub 0, exe 0x472100) that sweeps
// its gun, turns it onto the player and lobs bombs at him. See content/saul_kada/st3b_bomb_turret.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

void bombTurretUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
