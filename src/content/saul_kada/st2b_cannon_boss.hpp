// The boss of the pirates' mine (ST2B, story 12): the cannon turret (pool 0x20 type 0x14 variant 0, exe 0x473320),
// see content/saul_kada/st2b_cannon_boss.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x473320: a fixed cannon turret: its barrel turns / pitches toward the player, aimed shells and high lobs; a boss
// gauge; dying: blasts, the turret falls off, the item, a message, a Servbot jumps out
void cannonBossUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
