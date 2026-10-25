// ST24's script 0x56 (Pokte's first visit), content/calbania/st24_script56.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// Script 0x56, dispatched by the handler [0x89b6d0 + id * 4]
bool st24Script56Ported(uint8_t id);
bool st24Script56Tick(const PlayerEnv& env, uint8_t id);

} // namespace od::port
