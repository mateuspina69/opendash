// Roll at the Abandoned Mine's entrance, class 0x4e6110 (ST0F pool 0x20 type 0x60; content/yosyonke/abandoned_mine_roll.cpp).
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4e6110: a guided NPC -- stands and talks, or walks in along a path
void abandonedMineRollUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
