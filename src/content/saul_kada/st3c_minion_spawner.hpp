// The gold-statue fight's minion spawner (ST3C pool 0xe0 type 0x27, exe 0x427740), see
// content/saul_kada/st3c_minion_spawner.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

constexpr uint8_t kMinionSpawnerType = 0x27;

// exe 0x427740: spawns up to 3 of the fight's minions (content/saul_kada/st3c_minion) near the player on a timer (the
// boss spawns it)
void minionSpawnerUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
