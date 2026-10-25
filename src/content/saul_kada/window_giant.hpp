// The giant seen through the window (ST26 sub 5, story 13; pool 0x20 type 0x45 sub 2, exe 0x4cb980), see
// content/saul_kada/window_giant.cpp. The ST26 enemy spawner (content/saul_kada/enemy_spawners) places it.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

constexpr uint8_t kWindowGiantType = 0x45, kWindowGiantSubtype = 2;

// exe 0x4cb980: the giant walking past the window, breaking the wall segment of cell (0x44, 0x40) / (0x3c, 0x40)
// (flags 0x62 / 0x63) at frame 0x34 of its animation 0x41 / 0x44
void windowGiantUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
