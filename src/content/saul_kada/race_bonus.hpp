// The race's time-bonus pickup (static ST59 / 5A / 5B type 0x62, exe 0x5bf630), content/saul_kada/race_bonus.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5bf630 (static ST59 / 5A / 5B type 0x62): a floating time-bonus pickup of the race (freezes the clock)
void raceBonusUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
