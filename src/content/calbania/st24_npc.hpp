// ST24 talking route-walker NPC, class exe 0x4440f0 (pool 0x20), content/calbania/st24_npc.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4440f0 (pool 0x20, ST24 sub 0): a sibling of the town NPC 0x441950 with its own tables (boxes 0x88de10..,
// speeds 0x88de38, look tables 0x88e0ec.. / 0x88e11c): route walker or stationary NPC, talk, stop in front of the
// player; its spawn byte: no move test (0x40), no ground snap / solids (0x80), the scale 0x100 + (& 0x3f) * 8
void st24RouteNpcUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
