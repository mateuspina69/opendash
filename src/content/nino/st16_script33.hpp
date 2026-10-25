// ST16's script 0x33 (the player caught by the box carrier), content/nino/st16_script33.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// Script 0x33, dispatched by the handler [0x89b6d0 + id * 4]
bool st16Script33Ported(uint8_t id);
bool st16Script33Tick(const PlayerEnv& env, uint8_t id);

} // namespace od::port
