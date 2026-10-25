// The ice ruin's herded creature (ST2F pool 0x20 type 0x34 sub 5, exe 0x4a7b70; the room handler of ST2F sub 1,
// content/yosyonke/ice_ruin_rooms, places three), content/yosyonke/ice_ruin_herded_creature.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4a7b70: runs at / around the player, bounces off walls and neighbours, falls into the three pens (flags 0x275 /
// 0x276 / 0x277 by quadrant; a pen already taken: an explosion and its drops); variant 1 placed penned already
void herdedCreatureUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
