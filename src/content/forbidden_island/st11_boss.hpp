// ST11 (story 1, sub 0): the fight actor 0x4b3b10 (pool 0x20 type 0x3b sub 0), spawned by script 0x2a's records
// 0x8a4a58 (slot 0; content/forbidden_island/st11_script2a) and tracked by the room handler 0x57e180 (content/forbidden_island/island_visit: its slot-0 actor
// gone -> flag 0x592, the exit lights open). See content/forbidden_island/st11_boss.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// 0x4b3b10: pool 0x20 type 0x3b sub 0 -- approach / circle / hop attacks / knockback, boss gauge 0x415d30; death ->
// explosion (static type 8), drops, freed
void st11FighterUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
