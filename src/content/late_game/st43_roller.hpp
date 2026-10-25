// ST43's roller (static type 7, exe 0x5ad7b0), content/late_game/st43_roller.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5ad7b0 (ST43 static type 7, stage table 0x8e0824): a child of a spawner (its bit 4 + variant of the spawner's
// first parameter): rises in, wanders / turns to the player / rolls over; liftable (carried, thrown / dropped), hurt by
// hits, dies with a blast
void rollerUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
