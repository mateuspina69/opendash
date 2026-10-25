// The ST48 charger (pool 0x20 type 6 sub 0, exe 0x4538c0), see content/saul_kada/st48_charger.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4538c0: a wheeled enemy of the Saul Kada pirate attack -- it circles, boosts with two jets and charges the
// player, hops, skids round, bounces off walls, sidesteps up close; a heavy hit knocks it over (it gets up, then stays
// overturned); it dies in blasts and drops
void chargerUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
