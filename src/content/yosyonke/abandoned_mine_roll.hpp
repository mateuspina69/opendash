// Roll at the Abandoned Mine's entrance, class 0x4e6110 (ST0F pool 0x20 type 0x60; content/yosyonke/abandoned_mine_roll.cpp).
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4e6110: a guided NPC -- stands and talks, or walks in along a path
void abandonedMineRollUpdate(const PlayerEnv& env, uint32_t actor);
// a session (ContentRegistry::netPictures): where she is in her walk -- the world's, its copies', a scene's rebuilt one
std::vector<uint8_t> abandonedMineRollPicture(uint32_t actor);
void abandonedMineRollTakePicture(uint32_t actor, std::span<const uint8_t> picture);
void abandonedMineRollLayPicture(uint32_t actor, std::span<const uint8_t> picture);

} // namespace od::port
