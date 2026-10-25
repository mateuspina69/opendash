// Tron Bonne's tank (ST25 sub 6, story 3): the boss after script 0x1b. See content/calbania/tron_boss.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// 0x46d7e0 (pool 0x20, the ST25 stage table type 0x11 sub 0, spawned by the room handler 0x581bb0 from record
// 0x8c9220): the fight (attacks chosen by distance and RNG, hit points with Roll's / Tron's messages in window 4,
// arms that break off, death -> flag 0x589)
void tronBossUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
