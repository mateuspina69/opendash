// ST11's script 0x2a (the arrival on the Forbidden Island) and its cast, content/forbidden_island/st11_script2a.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

void script2aArrivalCastUpdate(const PlayerEnv& env, uint32_t actor); // ST11 pool 0x20 type 0x3b sub 1 (script 0x2a)

// Script 0x2a, dispatched by the handler [0x89b6d0 + id * 4]
bool st11Script2aPorted(uint8_t id);
bool st11Script2aTick(const PlayerEnv& env, uint8_t id);

} // namespace od::port
